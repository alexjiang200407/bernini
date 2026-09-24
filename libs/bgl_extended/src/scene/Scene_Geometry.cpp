#include "scene/GeomRollback.h"
#include "scene/Scene.h"
#include "types/VertexGen.h"
#include "util/util.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/GeomHandle.h>
#include <bgl/GeomType.h>
#include <bgl/GrassHandle.h>
#include <bgl/IScene.h>
#include <bgl/MaterialHandle.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/RigHandle.h>
#include <bgl_common/gassert.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/Meshlet.h>
#include <bgl_common/idl/MeshletGroup.h>
#include <bgl_common/idl/RawRange.h>
#include <bgl_common/idl/VertexLayout.h>
#include <cmath>
#include <core/containers/multi_slot_handle.h>
#include <core/containers/slot_handle.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#include <numbers>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		constexpr uint32_t c_MaxDispatchMeshGroups = 65535;

		// The static tier dispatches whole meshlet groups, so what it can launch is the largest
		// multiple of the group size that fits. A submesh past it would round its last group up over
		// the ceiling.
		constexpr uint32_t c_MaxSubmeshMeshlets =
			c_MaxDispatchMeshGroups - (c_MaxDispatchMeshGroups % idl::cMeshletsPerGroup);

		// A grass field dispatches one amplification group per chunk.
		constexpr uint32_t c_MaxGrassChunks = c_MaxDispatchMeshGroups;

		// One number declared twice, because bgl does not link assetlib: the cook groups by its
		// constant and everything below reads by this one. A drift would have a submesh read bounds
		// fitted to another submesh's meshlets, which is geometry dropped with pixels on screen --
		// so it is a compile error here rather than anything a container could carry past.
		static_assert(assetlib::c_MeshletsPerGroup == idl::cMeshletsPerGroup);

		/**
		 * The bound each run of `idl::cMeshletsPerGroup` meshlets is culled by, folded out of the
		 * meshlet spheres.
		 *
		 * What a submesh with no cooked bounds gets: a procedural primitive, whose meshlets are built
		 * here and never pass through a cook, or a `BMesh` assembled in memory. It encloses the same
		 * geometry the cook's fit does, just less tightly -- a sphere over spheres carries a meshlet
		 * radius of slack at every extreme.
		 */
		std::vector<idl::MeshletGroup>
		FoldMeshletGroups(std::span<const idl::Meshlet> meshlets)
		{
			std::vector<idl::MeshletGroup> groups;
			groups.reserve((meshlets.size() + idl::cMeshletsPerGroup - 1) / idl::cMeshletsPerGroup);

			for (size_t first = 0; first < meshlets.size(); first += idl::cMeshletsPerGroup)
			{
				const std::span<const idl::Meshlet> run = meshlets.subspan(
					first,
					std::min<size_t>(idl::cMeshletsPerGroup, meshlets.size() - first));

				auto lo = glm::vec3(std::numeric_limits<float>::max());
				auto hi = glm::vec3(std::numeric_limits<float>::lowest());
				for (const idl::Meshlet& meshlet : run)
				{
					const auto centre = glm::vec3(meshlet.boundingSphere);
					lo                = glm::min(lo, centre - meshlet.boundingSphere.w);
					hi                = glm::max(hi, centre + meshlet.boundingSphere.w);
				}

				const glm::vec3 centre = (lo + hi) * 0.5f;

				float radius = 0.0f;
				for (const idl::Meshlet& meshlet : run)
				{
					radius = std::max(
						radius,
						glm::distance(centre, glm::vec3(meshlet.boundingSphere)) +
							meshlet.boundingSphere.w);
				}

				auto group           = idl::MeshletGroup();
				group.boundingSphere = glm::vec4(centre, radius);
				groups.emplace_back(group);
			}

			return groups;
		}

		// assetlib_structs is data by rule, so a question about a layout is answered in assetlib,
		// which this does not link. The layout is a small fixed array and this is the whole of it.
		bool
		HasSkinBinding(const assetlib::VertexLayout& layout) noexcept
		{
			bool joints  = false;
			bool weights = false;
			for (uint8_t i = 0; i < layout.attributeCount; ++i)
			{
				joints |= layout.attributes[i].semantic == assetlib::VertexSemantic::kJoints0;
				weights |= layout.attributes[i].semantic == assetlib::VertexSemantic::kWeights0;
			}
			return joints && weights;
		}

		// The interleaved vertex layout the procedural geometry emits: position,
		// normal, uv, tangent, tightly packed at a 48-byte stride. This is exactly
		// the full VertexGen, and is decoded on the GPU via each submesh's
		// VertexLayout descriptor.
		constexpr uint32_t c_ProceduralStride = 48;
		static_assert(sizeof(VertexGen) == c_ProceduralStride);

		idl::VertexLayout
		MakeProceduralLayout()
		{
			auto layout           = idl::VertexLayout();
			layout.attributeCount = 4;
			layout.stride         = c_ProceduralStride;
			layout.attributes[0]  = { idl::VertexSemantic::kPosition,
				                      idl::VertexFormat::kFloat32x3,
				                      0 };
			layout.attributes[1]  = { idl::VertexSemantic::kNormal,
				                      idl::VertexFormat::kFloat32x3,
				                      12 };
			layout.attributes[2]  = { idl::VertexSemantic::kTexCoord0,
				                      idl::VertexFormat::kFloat32x2,
				                      24 };
			layout.attributes[3]  = { idl::VertexSemantic::kTangent,
				                      idl::VertexFormat::kFloat32x4,
				                      32 };
			return layout;
		}

		// A meshletized primitive: the meshlets, and the two pools they index into.
		struct MeshletBuild
		{
			std::vector<idl::Meshlet> meshlets;
			std::vector<uint32_t>     vertexMap;     // meshlet-local slot -> geometry vertex
			std::vector<uint32_t>     localIndices;  // meshlet-local slots, 3 per triangle
		};

		// (center, radius) circumscribing the box, so it is conservative for whatever the box held.
		glm::vec4
		BoundingSphereOf(const glm::vec3& minBound, const glm::vec3& maxBound) noexcept
		{
			const glm::vec3 center = (minBound + maxBound) * 0.5f;
			return glm::vec4(center, glm::distance(maxBound, center));
		}

		/**
		 * Greedily packs `indices` into meshlets, in triangle order, filling each one until the next
		 * triangle would push it past cMaxVerticesPerMeshlet or cMaxPrimsPerMeshlet.
		 *
		 * Those two caps are the mesh shader's output-array sizes, so a meshlet that overruns either
		 * renders garbage rather than failing. Each meshlet therefore remaps the vertices it touches to
		 * a local slot; a vertex shared across meshlets is simply stored in each of them.
		 */
		MeshletBuild
		BuildMeshlets(std::span<const VertexGen> verts, std::span<const uint32_t> indices)
		{
			auto build = MeshletBuild();

			const uint32_t totalTriangles = static_cast<uint32_t>(indices.size() / 3u);
			uint32_t       trianglesDone  = 0u;

			while (trianglesDone < totalTriangles)
			{
				auto meshlet                 = idl::Meshlet();
				meshlet.relativeVertexOffset = static_cast<uint32_t>(build.vertexMap.size());
				meshlet.relativeIndexOffset  = static_cast<uint32_t>(build.localIndices.size());

				std::unordered_map<uint32_t, uint32_t> localRemap;
				uint32_t                               localVertexCount   = 0u;
				uint32_t                               localTriangleCount = 0u;

				while (trianglesDone < totalTriangles)
				{
					const uint32_t triBase = trianglesDone * 3u;
					const uint32_t tri[3]  = { indices[triBase],
						                       indices[triBase + 1u],
						                       indices[triBase + 2u] };

					uint32_t newVertices = 0u;
					for (uint32_t i = 0u; i < 3u; ++i)
					{
						if (!localRemap.contains(tri[i]))
						{
							++newVertices;
						}
					}

					if (localVertexCount + newVertices > idl::cMaxVerticesPerMeshlet ||
					    localTriangleCount + 1u > idl::cMaxPrimsPerMeshlet)
					{
						break;
					}

					for (uint32_t i = 0u; i < 3u; ++i)
					{
						const uint32_t geomVertexIdx = tri[i];
						if (!localRemap.contains(geomVertexIdx))
						{
							localRemap[geomVertexIdx] = localVertexCount++;
							build.vertexMap.push_back(geomVertexIdx);
						}
						build.localIndices.push_back(localRemap[geomVertexIdx]);
					}

					++localTriangleCount;
					++trianglesDone;
				}

				meshlet.vertexCount   = localVertexCount;
				meshlet.triangleCount = localTriangleCount;

				auto minBound = glm::vec3(std::numeric_limits<float>::max());
				auto maxBound = glm::vec3(std::numeric_limits<float>::lowest());
				for (const auto& [geomVertexIdx, localIdx] : localRemap)
				{
					minBound = glm::min(minBound, verts[geomVertexIdx].pos);
					maxBound = glm::max(maxBound, verts[geomVertexIdx].pos);
				}
				const glm::vec4 sphere = BoundingSphereOf(minBound, maxBound);
				meshlet.boundingSphere = sphere;

				build.meshlets.push_back(meshlet);
			}

			return build;
		}

		idl::VertexLayout
		ConvertLayout(const assetlib::VertexLayout& src)
		{
			// A vertex is loaded as typed values off this grid, and an offset that misses it reads
			// neighbouring bytes rather than failing.
			if (src.stride % 4 != 0)
			{
				core::throw_runtime_error(
					"A vertex layout's stride ({}) must be a multiple of 4 bytes",
					src.stride);
			}

			auto dst           = idl::VertexLayout();
			dst.attributeCount = src.attributeCount;
			dst.stride         = src.stride;
			for (uint32_t i = 0; i < src.attributeCount; ++i)
			{
				dst.attributes[i].semantic =
					static_cast<idl::VertexSemantic>(src.attributes[i].semantic);
				dst.attributes[i].format = static_cast<idl::VertexFormat>(src.attributes[i].format);

				if (src.attributes[i].offset % 4 != 0)
				{
					core::throw_runtime_error(
						"A vertex attribute's offset ({}) must be a multiple of 4 bytes",
						src.attributes[i].offset);
				}
				dst.attributes[i].byteOffset = src.attributes[i].offset;
			}
			return dst;
		}
	}

	GeomHandle
	Scene::AddProceduralGeom(
		std::span<const VertexGen>     verts,
		std::span<const uint32_t>      indices,
		MaterialHandle                 material,
		const std::optional<glm::vec4> boundingSphere)
	{
		const auto build = BuildMeshlets(verts, indices);

		// A procedural primitive is one submesh, so its meshlets all have to fit in a single
		// dispatch.
		if (build.meshlets.size() > c_MaxSubmeshMeshlets)
		{
			throw SceneError(
				"Scene::AddProceduralGeom: the primitive needs " +
				std::to_string(build.meshlets.size()) + " meshlets, over the " +
				std::to_string(c_MaxSubmeshMeshlets) + " a single dispatch can launch");
		}

		try
		{
			// Nothing below is the scene's until Commit(); see GeomRollback. The fallback sphere the
			// editor shows after a failed load goes through here, so a leak here is what would take
			// the fallback down too.
			auto rollback = GeomRollback();

			const auto baseVertexGlobal = rollback.Track(
				m_VertexDataBuffer,
				m_VertexDataBuffer.AddBytes(std::as_bytes(verts)));
			const auto baseMapGlobal =
				rollback.Track(m_VertexMapBuffer, m_VertexMapBuffer.Add(build.vertexMap));
			const auto baseIndexGlobal =
				rollback.Track(m_IndexBuffer, m_IndexBuffer.Add(build.localIndices));
			const auto baseMeshletGlobal =
				rollback.Track(m_MeshletBuffer, m_MeshletBuffer.Add(build.meshlets));

			const std::vector<idl::MeshletGroup> groups = FoldMeshletGroups(build.meshlets);
			const auto                           baseGroupGlobal =
				rollback.Track(m_MeshletGroupBuffer, m_MeshletGroupBuffer.Add(groups));

			auto submesh          = idl::Submesh();
			submesh.layout        = MakeProceduralLayout();
			submesh.meshlets      = baseMeshletGlobal;
			submesh.meshletGroups = baseGroupGlobal;
			submesh.vertexMap     = baseMapGlobal;
			submesh.vertexData    = baseVertexGlobal;
			submesh.indices       = baseIndexGlobal;
			submesh.vertexCount   = static_cast<uint32_t>(verts.size());

			// An animated geom overrides the fold: its vertices move every frame, so the sphere must
			// come from the clip set's posed bounds rather than the bind pose uploaded here.
			if (boundingSphere.has_value())
			{
				submesh.boundingSphere = *boundingSphere;
			}
			else if (!verts.empty())
			{
				auto minBound = glm::vec3(std::numeric_limits<float>::max());
				auto maxBound = glm::vec3(std::numeric_limits<float>::lowest());
				for (const VertexGen& v : verts)
				{
					minBound = glm::min(minBound, v.pos);
					maxBound = glm::max(maxBound, v.pos);
				}

				const glm::vec4 sphere = BoundingSphereOf(minBound, maxBound);
				submesh.boundingSphere = sphere;
			}

			const auto submeshSpan = std::span<const idl::Submesh>(&submesh, 1);
			const auto baseSubmeshGlobal =
				rollback.Track(m_SubmeshBuffer, m_SubmeshBuffer.Add(submeshSpan));

			m_SubmeshBuffer.MetaAt(baseSubmeshGlobal.index) = SubmeshDefaults{ material };

			auto submeshRange = idl::RangeWithCount();
			submeshRange      = baseSubmeshGlobal;

			auto retVal     = GeomHandle();
			retVal.handle   = AllocateGeomSlot(GeomRecord{ .submeshes = submeshRange });
			retVal.geomType = GeomType::kStaticMesh;

			// The geom owns its ranges now, and DeleteGeom is what gives them back.
			rollback.Commit();

			return retVal;
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	GeomHandle
	Scene::AddSkinnedMeshGeom(
		const assetlib::BMesh&          mesh,
		uint32_t                        meshIndex,
		std::span<const MaterialHandle> materials,
		RigHandle                       rig,
		const assetlib::Bounds&         posedBounds)
	{
		const RigMeta* meta = FindRig(rig);
		if (meta == nullptr)
		{
			throw SceneError("AddSkinnedMeshGeom: rig is null, or names a rig already deleted");
		}

		// Read out before the geometry is built: the metadata lives in a vector another Add may
		// reallocate, and nothing below needs the pointer again.
		const uint32_t rigBoneCount = meta->boneCount;
		const uint32_t rigClipCount = meta->clipCount;
		const uint32_t rigNodeCount = meta->nodeCount;
		const uint32_t rigLegCount  = meta->legCount;

		if (glm::any(glm::greaterThan(posedBounds.min, posedBounds.max)))
		{
			throw SceneError("AddSkinnedMeshGeom: posedBounds min exceeds max");
		}

		if (meshIndex >= mesh.meshes.size())
		{
			throw SceneError("AddSkinnedMeshGeom: meshIndex out of range");
		}

		const assetlib::Mesh& entry = mesh.meshes[meshIndex];
		for (uint32_t s = 0; s < entry.submeshCount; ++s)
		{
			const assetlib::Submesh& submesh = mesh.submeshes[entry.firstSubmesh + s];

			if (!HasSkinBinding(submesh.layout))
			{
				throw SceneError(
					"AddSkinnedMeshGeom: every submesh needs joints0 and weights0 -- one without "
					"skin binding would hold its bind pose while the rest of the mesh moved");
			}

			const uint32_t       index = submesh.material;
			const MaterialHandle bound =
				index < materials.size() ? materials[index] : MaterialHandle{};
			if (!AcceptsMaterial(GeomType::kSkinnedMesh, bound))
			{
				throw SceneError(
					"AddSkinnedMeshGeom: every submesh needs a baked PBR or a game surface "
					"material -- the skinned pipeline has no unlit or loose variant");
			}
		}

		GeomHandle base = AddPreparedMesh(
			CookStaticMesh(mesh, meshIndex),
			materials,
			{},
			BoundingSphereOf(posedBounds.min, posedBounds.max));

		GeomRecord& geom = m_Geoms[base.handle.index];
		geom.rig         = rig.handle;
		geom.clipCount   = rigClipCount;
		geom.nodeCount   = rigNodeCount;
		geom.boneCount   = rigBoneCount;
		geom.legCount    = rigLegCount;

		// Last, so nothing above can throw with the use already counted.
		RigMeta* counted = FindRig(rig);
		gassert(counted != nullptr, "the rig validated above went away mid-add");
		++counted->useCount;

		base.geomType = GeomType::kSkinnedMesh;
		return base;
	}

	GeomHandle
	Scene::AddCubeGeom(MaterialHandle material)
	{
		// 6 faces x 4 verts (24 total) so each face carries its own normal, uv
		// and tangent -- an 8-vertex cube can't express per-face attributes.
		struct FaceBasis
		{
			glm::vec3 normal;
			glm::vec3 tangent;  // +u direction; bitangent = cross(normal, tangent)
		};
		static const FaceBasis c_Faces[6] = {
			{ { 1, 0, 0 }, { 0, 0, -1 } },   // +X
			{ { -1, 0, 0 }, { 0, 0, 1 } },   // -X
			{ { 0, 1, 0 }, { 1, 0, 0 } },    // +Y
			{ { 0, -1, 0 }, { 1, 0, 0 } },   // -Y
			{ { 0, 0, 1 }, { 1, 0, 0 } },    // +Z
			{ { 0, 0, -1 }, { -1, 0, 0 } },  // -Z
		};
		// Per-face corners in (s, t) order: BL, BR, TR, TL -- CCW from outside.
		static const glm::vec2 c_Corners[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

		std::vector<VertexGen> cubeVertices;
		std::vector<uint32_t>  cubeIndices;
		cubeVertices.reserve(24);
		cubeIndices.reserve(36);

		for (const auto& face : c_Faces)
		{
			const glm::vec3 up   = glm::cross(face.normal, face.tangent);
			const uint32_t  base = static_cast<uint32_t>(cubeVertices.size());

			for (const auto& c : c_Corners)
			{
				auto v    = VertexGen();
				v.pos     = face.normal + c.x * face.tangent + c.y * up;
				v.normal  = face.normal;
				v.uv      = glm::vec2((c.x + 1.0f) * 0.5f, (c.y + 1.0f) * 0.5f);
				v.tangent = glm::vec4(face.tangent, 1.0f);
				cubeVertices.push_back(v);
			}

			cubeIndices.push_back(base + 0u);
			cubeIndices.push_back(base + 1u);
			cubeIndices.push_back(base + 2u);
			cubeIndices.push_back(base + 0u);
			cubeIndices.push_back(base + 2u);
			cubeIndices.push_back(base + 3u);
		}

		return AddProceduralGeom(cubeVertices, cubeIndices, material);
	}

	GeomHandle
	Scene::AddSphereGeom(
		uint32_t       xSegments,
		uint32_t       ySegments,
		float          radius,
		MaterialHandle material)
	{
		if (xSegments == 0u || ySegments == 0u)
		{
			throw SceneError(
				"Scene::AddSphereGeom: xSegments and ySegments must both be at least 1");
		}

		std::vector<VertexGen> sphereVerts;
		std::vector<uint32_t>  sphereIndices;

		for (uint32_t y = 0u; y <= ySegments; ++y)
		{
			for (uint32_t x = 0u; x <= xSegments; ++x)
			{
				constexpr auto c_Pi     = std::numbers::pi_v<float>;
				float          xSegment = static_cast<float>(x) / static_cast<float>(xSegments);
				float          ySegment = static_cast<float>(y) / static_cast<float>(ySegments);
				float          xPos = std::cos(xSegment * 2.0f * c_Pi) * std::sin(ySegment * c_Pi);
				float          yPos = std::cos(ySegment * c_Pi);
				float          zPos = std::sin(xSegment * 2.0f * c_Pi) * std::sin(ySegment * c_Pi);

				// Tangent follows +u (increasing longitude): d(pos)/d(xSegment),
				// normalized. bitangent = cross(normal, tangent), so w = +1.
				const float a = xSegment * 2.0f * c_Pi;

				auto v   = VertexGen();
				v.pos    = glm::vec3(xPos, yPos, zPos) * radius;
				v.normal = glm::normalize(v.pos);
				v.uv     = glm::vec2(xSegment, ySegment);
				v.tangent =
					glm::vec4(glm::normalize(glm::vec3(-std::sin(a), 0.0f, std::cos(a))), 1.0f);
				sphereVerts.push_back(v);
			}
		}

		for (uint32_t y = 0u; y < ySegments; ++y)
		{
			for (uint32_t x = 0u; x < xSegments; ++x)
			{
				sphereIndices.push_back((y + 1u) * (xSegments + 1u) + x);
				sphereIndices.push_back(y * (xSegments + 1u) + x);
				sphereIndices.push_back(y * (xSegments + 1u) + x + 1u);

				sphereIndices.push_back((y + 1u) * (xSegments + 1u) + x);
				sphereIndices.push_back(y * (xSegments + 1u) + x + 1u);
				sphereIndices.push_back((y + 1u) * (xSegments + 1u) + x + 1u);
			}
		}

		return AddProceduralGeom(sphereVerts, sphereIndices, material);
	}

	GeomHandle
	Scene::AddPlaneGeom(
		uint32_t       xSegments,
		uint32_t       ySegments,
		float          width,
		float          height,
		MaterialHandle material)
	{
		if (xSegments == 0u || ySegments == 0u)
		{
			throw SceneError(
				"Scene::AddPlaneGeom: xSegments and ySegments must both be at least 1");
		}

		std::vector<VertexGen> planeVerts;
		std::vector<uint32_t>  planeIndices;
		planeVerts.reserve(static_cast<size_t>(xSegments + 1u) * (ySegments + 1u));
		planeIndices.reserve(static_cast<size_t>(xSegments) * ySegments * 6u);

		for (uint32_t y = 0u; y <= ySegments; ++y)
		{
			for (uint32_t x = 0u; x <= xSegments; ++x)
			{
				const float u = static_cast<float>(x) / static_cast<float>(xSegments);
				const float v = static_cast<float>(y) / static_cast<float>(ySegments);

				auto vert   = VertexGen();
				vert.pos    = glm::vec3((u - 0.5f) * width, (v - 0.5f) * height, 0.0f);
				vert.normal = glm::vec3(0.0f, 0.0f, 1.0f);
				vert.uv     = glm::vec2(u, v);

				// +u runs along +X, so the tangent is +X. The bitangent has to come out along +v,
				// which here is +Y, and cross(+Z, +X) is +Y -- so the handedness is +1. The wrong
				// sign here inverts every normal map's green channel, silently.
				vert.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
				planeVerts.push_back(vert);
			}
		}

		const uint32_t rowStride = xSegments + 1u;
		for (uint32_t y = 0u; y < ySegments; ++y)
		{
			for (uint32_t x = 0u; x < xSegments; ++x)
			{
				const uint32_t i00 = y * rowStride + x;
				const uint32_t i10 = i00 + 1u;
				const uint32_t i01 = i00 + rowStride;
				const uint32_t i11 = i01 + 1u;

				// Counter-clockwise seen from +Z, exactly like the cube's +Z face, so the quad faces
				// a camera looking down -Z at it.
				planeIndices.push_back(i00);
				planeIndices.push_back(i10);
				planeIndices.push_back(i11);

				planeIndices.push_back(i00);
				planeIndices.push_back(i11);
				planeIndices.push_back(i01);
			}
		}

		return AddProceduralGeom(planeVerts, planeIndices, material);
	}

	struct PreparedStaticMesh::Impl
	{
		struct Submesh
		{
			std::vector<std::byte>         vertexBytes;
			std::vector<uint32_t>          vertexMap;
			std::vector<uint32_t>          localIndices;
			std::vector<idl::Meshlet>      meshlets;
			std::vector<idl::MeshletGroup> meshletGroups;
			assetlib::VertexLayout         layout;
			uint32_t                       vertexCount    = 0;
			uint32_t                       material       = 0;
			glm::vec4                      boundingSphere = glm::vec4(0.0f);
		};

		/** One grass field of the mesh, its chunks' `firstClump` rebased onto `clumps`. */
		struct GrassField
		{
			uint32_t                          slot = 0;
			std::vector<assetlib::GrassChunk> chunks;
			std::vector<assetlib::GrassClump> clumps;
		};

		std::vector<Submesh>    submeshes;
		std::vector<GrassField> grassFields;
	};

	namespace
	{
		/**
		 * Copies mesh `meshIndex`'s grass fields out of the BMesh's pools. The ranges come from the
		 * file, so each is checked against the pool it names before anything is read.
		 */
		void
		CookGrassFields(
			const assetlib::BMesh&                             mesh,
			const uint32_t                                     meshIndex,
			std::vector<PreparedStaticMesh::Impl::GrassField>& out)
		{
			for (size_t f = 0; f < mesh.grass.fields.size(); ++f)
			{
				const assetlib::GrassField& src = mesh.grass.fields[f];
				if (src.mesh != meshIndex)
				{
					continue;
				}

				if (src.chunkCount == 0)
				{
					throw SceneError(
						std::format("CookStaticMesh: grass field {} has no chunks", f));
				}

				if (src.chunkCount > c_MaxGrassChunks)
				{
					throw SceneError(
						std::format(
							"CookStaticMesh: grass field {} has {} chunks, more than the {} thread "
							"groups one dispatch can launch",
							f,
							src.chunkCount,
							c_MaxGrassChunks));
				}

				if (static_cast<uint64_t>(src.firstChunk) + src.chunkCount >
				    mesh.grass.chunks.size())
				{
					throw SceneError(
						std::format(
							"CookStaticMesh: grass field {} claims {} chunks at offset {}, past "
							"the "
							"end of the mesh's {} of them",
							f,
							src.chunkCount,
							src.firstChunk,
							mesh.grass.chunks.size()));
				}

				PreparedStaticMesh::Impl::GrassField& field = out.emplace_back();
				field.slot                                  = src.material;
				field.chunks.reserve(src.chunkCount);

				for (uint32_t c = 0; c < src.chunkCount; ++c)
				{
					assetlib::GrassChunk chunk = mesh.grass.chunks[src.firstChunk + c];
					if (chunk.clumpCount == 0 ||
					    chunk.clumpCount > assetlib::c_GrassClumpsPerChunk ||
					    static_cast<uint64_t>(chunk.firstClump) + chunk.clumpCount >
					        mesh.grass.clumps.size())
					{
						throw SceneError(
							std::format(
								"CookStaticMesh: grass field {} chunk {} holds no clumps, more "
								"than "
								"{}, or clumps past the end of the mesh's {}",
								f,
								c,
								assetlib::c_GrassClumpsPerChunk,
								mesh.grass.clumps.size()));
					}

					const auto first = mesh.grass.clumps.begin() + chunk.firstClump;
					chunk.firstClump = static_cast<uint32_t>(field.clumps.size());
					field.clumps.insert(field.clumps.end(), first, first + chunk.clumpCount);
					field.chunks.emplace_back(chunk);
				}
			}
		}
	}

	PreparedStaticMesh::PreparedStaticMesh() noexcept                     = default;
	PreparedStaticMesh::~PreparedStaticMesh()                             = default;
	PreparedStaticMesh::PreparedStaticMesh(PreparedStaticMesh&&) noexcept = default;
	PreparedStaticMesh&
	PreparedStaticMesh::operator=(PreparedStaticMesh&&) noexcept = default;

	PreparedStaticMesh
	CookStaticMesh(const assetlib::BMesh& mesh, uint32_t meshIndex)
	{
		if (meshIndex >= mesh.meshes.size())
		{
			throw SceneError("CookStaticMesh: meshIndex out of range");
		}

		const assetlib::Mesh& meshEntry = mesh.meshes[meshIndex];

		auto impl = std::make_unique<PreparedStaticMesh::Impl>();
		impl->submeshes.reserve(meshEntry.submeshCount);

		for (uint32_t s = 0; s < meshEntry.submeshCount; ++s)
		{
			const assetlib::Submesh& src = mesh.submeshes[meshEntry.firstSubmesh + s];

			if (src.meshletCount == 0 || src.vertexCount == 0)
			{
				throw SceneError(std::format("CookStaticMesh: submesh {} has no geometry", s));
			}

			if (src.meshletCount > c_MaxSubmeshMeshlets)
			{
				throw SceneError(
					std::format(
						"CookStaticMesh: submesh {} has {} meshlets, more than the {} thread "
						"groups a mesh dispatch can launch",
						s,
						src.meshletCount,
						c_MaxSubmeshMeshlets));
			}

			const uint64_t vertexByteCount =
				static_cast<uint64_t>(src.vertexCount) * src.layout.stride;

			// The offsets and counts come from the file, so they are the caller's claim about the
			// buffers, not a fact about them. Trusting them would read off the end of a truncated
			// or malformed .bmesh.
			if (src.vertexByteOffset + vertexByteCount > mesh.vertexData.size())
			{
				throw SceneError(
					std::format(
						"CookStaticMesh: submesh {} claims {} bytes of vertex data at offset {}, "
						"past the end of the mesh's {}-byte vertex buffer",
						s,
						vertexByteCount,
						src.vertexByteOffset,
						mesh.vertexData.size()));
			}

			PreparedStaticMesh::Impl::Submesh& out = impl->submeshes.emplace_back();
			out.layout                             = src.layout;
			out.vertexCount                        = src.vertexCount;
			out.material                           = src.material;
			out.boundingSphere                     = BoundingSphereOf(src.aabbMin, src.aabbMax);

			out.vertexBytes.resize(vertexByteCount);
			std::memcpy(
				out.vertexBytes.data(),
				mesh.vertexData.data() + src.vertexByteOffset,
				vertexByteCount);

			uint32_t mapCount   = 0;
			uint32_t indexCount = 0;
			for (uint32_t m = 0; m < src.meshletCount; ++m)
			{
				const assetlib::Meshlet& ml = mesh.meshlets[src.firstMeshlet + m];

				// A loose bound, not a truncation guard -- both sides' counts are uint32.
				// What the mesh stage can actually emit is cMaxVerticesPerMeshlet /
				// cMaxPrimsPerMeshlet, and only PrepareMeshlet checks that.
				if (ml.vertexCount > std::numeric_limits<uint16_t>::max() ||
				    ml.triangleCount > std::numeric_limits<uint16_t>::max() ||
				    static_cast<uint64_t>(ml.vertexOffset) + ml.vertexCount >
				        mesh.meshletVertices.size() ||
				    static_cast<uint64_t>(ml.triangleOffset) + ml.triangleCount * 3ull >
				        mesh.meshletTriangles.size())
				{
					throw SceneError(
						std::format(
							"CookStaticMesh: submesh {} meshlet {} overflows its streams or the "
							"meshlet count bound",
							s,
							m));
				}

				mapCount += ml.vertexCount;
				indexCount += ml.triangleCount * 3u;
			}

			const uint32_t groupCount =
				(src.meshletCount + idl::cMeshletsPerGroup - 1u) / idl::cMeshletsPerGroup;

			// A mesh assembled in memory rather than baked carries none, and gets the fold below.
			const bool cooked = !mesh.meshletGroups.empty();

			// The offset and the count come from the file, like the vertex ranges above.
			if (cooked && static_cast<uint64_t>(src.firstMeshletGroup) + groupCount >
			                  mesh.meshletGroups.size())
			{
				throw SceneError(
					std::format(
						"CookStaticMesh: submesh {} claims {} meshlet group bounds at offset {}, "
						"past the end of the mesh's {} of them",
						s,
						groupCount,
						src.firstMeshletGroup,
						mesh.meshletGroups.size()));
			}

			out.vertexMap.reserve(mapCount);
			out.localIndices.reserve(indexCount);
			out.meshlets.reserve(src.meshletCount);
			out.meshletGroups.reserve(groupCount);

			for (uint32_t m = 0; m < src.meshletCount; ++m)
			{
				const assetlib::Meshlet& ml = mesh.meshlets[src.firstMeshlet + m];

				auto meshlet                 = idl::Meshlet();
				meshlet.relativeVertexOffset = static_cast<uint32_t>(out.vertexMap.size());
				meshlet.relativeIndexOffset  = static_cast<uint32_t>(out.localIndices.size());
				meshlet.vertexCount          = static_cast<uint16_t>(ml.vertexCount);
				meshlet.triangleCount        = static_cast<uint16_t>(ml.triangleCount);
				meshlet.boundingSphere       = glm::vec4(ml.boundingCenter, ml.boundingRadius);
				out.meshlets.push_back(meshlet);

				out.vertexMap.insert(
					out.vertexMap.end(),
					mesh.meshletVertices.begin() + ml.vertexOffset,
					mesh.meshletVertices.begin() + ml.vertexOffset + ml.vertexCount);

				// Widened one element at a time: the triangle stream is byte-sized.
				const uint32_t triangleIndices = ml.triangleCount * 3u;
				for (uint32_t i = 0; i < triangleIndices; ++i)
				{
					out.localIndices.push_back(mesh.meshletTriangles[ml.triangleOffset + i]);
				}
			}

			if (cooked)
			{
				for (uint32_t g = 0; g < groupCount; ++g)
				{
					const assetlib::MeshletGroup& bound =
						mesh.meshletGroups[src.firstMeshletGroup + g];

					auto group           = idl::MeshletGroup();
					group.boundingSphere = glm::vec4(bound.boundingCenter, bound.boundingRadius);
					out.meshletGroups.emplace_back(group);
				}
			}
			else
			{
				out.meshletGroups = FoldMeshletGroups(out.meshlets);
			}
		}

		CookGrassFields(mesh, meshIndex, impl->grassFields);

		auto prepared   = PreparedStaticMesh();
		prepared.m_Impl = std::move(impl);
		return prepared;
	}

	GeomHandle
	Scene::AddStaticMeshGeom(
		const assetlib::BMesh&          mesh,
		uint32_t                        meshIndex,
		std::span<const MaterialHandle> materials,
		std::span<const GrassHandle>    grass)
	{
		return AddStaticMeshGeom(CookStaticMesh(mesh, meshIndex), materials, grass);
	}

	GeomHandle
	Scene::AddStaticMeshGeom(
		PreparedStaticMesh              mesh,
		std::span<const MaterialHandle> materials,
		std::span<const GrassHandle>    grass)
	{
		return AddPreparedMesh(std::move(mesh), materials, grass, std::nullopt);
	}

	GeomHandle
	Scene::AddPreparedMesh(
		PreparedStaticMesh              mesh,
		std::span<const MaterialHandle> materials,
		std::span<const GrassHandle>    grass,
		const std::optional<glm::vec4>  sphereOverride)
	{
		try
		{
			if (mesh.m_Impl == nullptr || mesh.m_Impl->submeshes.empty())
			{
				throw SceneError(
					"AddStaticMeshGeom: the prepared mesh is empty or already consumed");
			}

			std::vector<GrassHandle> boundGrass;
			for (const PreparedStaticMesh::Impl::GrassField& field : mesh.m_Impl->grassFields)
			{
				const GrassHandle look =
					field.slot < grass.size() ? grass[field.slot] : GrassHandle{};
				if (!look.IsValid())
				{
					continue;
				}

				if (!IsGrassAlive(look))
				{
					throw SceneError(
						std::format(
							"AddStaticMeshGeom: the grass look bound to slot {} has been deleted",
							field.slot));
				}

				boundGrass.emplace_back(look);
			}

			// One GPU submesh per source submesh, in order: callers address geometry by source
			// submesh index (that is what an asset's material slots are numbered by), so the two
			// must stay 1:1.
			std::vector<idl::Submesh> submeshes;
			submeshes.reserve(mesh.m_Impl->submeshes.size());

			std::vector<MaterialHandle> defaults;
			defaults.reserve(mesh.m_Impl->submeshes.size());

			// Nothing below is the scene's until Commit(); see GeomRollback.
			auto rollback = GeomRollback();

			for (const PreparedStaticMesh::Impl::Submesh& src : mesh.m_Impl->submeshes)
			{
				auto submesh   = idl::Submesh();
				submesh.layout = ConvertLayout(src.layout);
				submesh.meshlets =
					rollback.Track(m_MeshletBuffer, m_MeshletBuffer.Add(src.meshlets));
				submesh.meshletGroups = rollback.Track(
					m_MeshletGroupBuffer,
					m_MeshletGroupBuffer.Add(src.meshletGroups));
				submesh.vertexMap =
					rollback.Track(m_VertexMapBuffer, m_VertexMapBuffer.Add(src.vertexMap));
				submesh.vertexData = rollback.Track(
					m_VertexDataBuffer,
					m_VertexDataBuffer.AddBytes(src.vertexBytes));
				submesh.indices =
					rollback.Track(m_IndexBuffer, m_IndexBuffer.Add(src.localIndices));
				submesh.vertexCount    = src.vertexCount;
				submesh.boundingSphere = sphereOverride.value_or(src.boundingSphere);

				submeshes.push_back(submesh);
				defaults.push_back(
					src.material < materials.size() ? materials[src.material] : MaterialHandle{});
			}

			const auto baseSubmeshGlobal = rollback.Track(
				m_SubmeshBuffer,
				m_SubmeshBuffer.Add(std::span<const idl::Submesh>(submeshes)));

			// Meta is keyed at the range root, so it can only be filed once the range is allocated.
			m_SubmeshBuffer.MetaAt(baseSubmeshGlobal.index) = std::move(defaults);

			// RangeWithCount is assignable from the buffer handle, but not constructible from it.
			auto submeshRange = idl::RangeWithCount();
			submeshRange      = baseSubmeshGlobal;

			auto retVal   = GeomHandle();
			retVal.handle = AllocateGeomSlot(
				GeomRecord{ .submeshes = submeshRange, .grass = std::move(boundGrass) });
			retVal.geomType = GeomType::kStaticMesh;

			// After the last throw, as AddSkinnedMeshGeom counts its rig: a use counted for a geom
			// that failed to build would refuse DeleteGrass forever.
			for (const GrassHandle look : m_Geoms[retVal.handle.index].grass)
			{
				++m_Grass[look.handle.index].useCount;
			}

			// The geom owns its ranges now, and DeleteGeom is what gives them back.
			rollback.Commit();

			return retVal;
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	void
	Scene::DeleteGeom(GeomHandle geom)
	{
		if (geom.geomType == GeomType::kInvalid || geom.geomType == GeomType::kCount)
		{
			throw SceneError("GeomHandle passed to DeleteGeom has no valid geom type");
		}

		if (!IsGeomAlive(geom))
		{
			throw SceneError("GeomHandle passed to DeleteGeom refers to a deleted or unknown geom");
		}

		const GeomRecord& record = m_Geoms[geom.handle.index];

		// A rig is shared, so this releases the geom's use of it and frees nothing. DeleteRig frees
		// the ranges, and refuses until every geom on the rig has been through here.
		if (record.rig)
		{
			RigMeta* rig = FindRig(RigHandle{ record.rig });
			gassert(rig != nullptr, "a live skinned geom names a rig that is already gone");

			if (rig != nullptr && rig->useCount > 0)
			{
				--rig->useCount;
			}
		}

		for (const GrassHandle look : record.grass)
		{
			gassert(IsGrassAlive(look), "a live geom binds a grass look that is already gone");
			if (IsGrassAlive(look) && m_Grass[look.handle.index].useCount > 0)
			{
				--m_Grass[look.handle.index].useCount;
			}
		}

		const auto& submeshes = record.submeshes;

		// The geometry's per-part ranges live on each Submesh, and each submesh owns its own, so
		// free them per submesh before releasing the submesh range itself.
		const uint32_t submeshRoot = submeshes.range.offsetStart;

		for (uint32_t i = 0; i < submeshes.count; ++i)
		{
			const auto& submesh = m_SubmeshBuffer.AtIndex(submeshRoot + i);

			m_VertexDataBuffer.Erase(submesh.vertexData.byteStart);
			m_VertexMapBuffer.EraseByIndex(submesh.vertexMap.offsetStart);
			m_IndexBuffer.EraseByIndex(submesh.indices.offsetStart);
			m_MeshletBuffer.EraseByIndex(submesh.meshlets.range.offsetStart);
			m_MeshletGroupBuffer.EraseByIndex(submesh.meshletGroups.offsetStart);
		}

		m_SubmeshBuffer.EraseByIndex(submeshRoot);
		m_GeomBuffer.Erase(record.entry);
		m_Geoms.release_slot(geom.handle.index);
	}
}
