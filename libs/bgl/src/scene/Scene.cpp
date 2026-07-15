#include "scene/Scene.h"
#include "fg/FrameGraph.h"
#include "idl/Constants.h"
#include "idl/Meshlet.h"
#include "types/SubmeshInstance.h"
#include "uniforms/Uniforms.h"
#include "util/util.h"
#include <assetlib_structs/BMaterial.h>  // the channel layout the static_asserts below pin us to
#include <bgl/PsoType.h>
#include <core/math.h>
#include <numbers>

namespace bgl
{
	namespace
	{
		constexpr uint32_t c_MaxDispatchMeshGroups = 65535;

		struct BufferInfo
		{
			std::string_view name;
		};

		// Order MUST stay in lockstep with Scene::GetBuffers() and with
		// ForwardPass's c_ForwardDataBuffers.
		static constexpr std::array<BufferInfo, 7> c_BufferInfo = {
			{ { "scene.submeshBuffer" },
			  { "scene.meshletBuffer" },
			  { "scene.vertexMapBuffer" },
			  { "scene.vertexDataBuffer" },
			  { "scene.indexBuffer" },
			  { "scene.pbrMaterialBuffer" },
			  { "scene.looseMaterialBuffer" } }
		};

		// The interleaved vertex layout the procedural geometry emits: position,
		// normal, uv, tangent, tightly packed at a 48-byte stride. This is exactly
		// the full VertexGen, and is decoded on the GPU via each submesh's
		// VertexLayout descriptor.
		constexpr uint32_t c_ProceduralStride   = 48;
		constexpr uint32_t c_ProceduralVtxWords = c_ProceduralStride / 4;

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

		// Interleave each vertex's position/normal/uv into the raw byte layout
		// above, returned as uint words for the StructuredBuffer<uint> data buffer.
		std::vector<uint32_t>
		PackVertices(std::span<const VertexGen> verts)
		{
			auto words = std::vector<uint32_t>(verts.size() * c_ProceduralVtxWords);
			for (size_t i = 0; i < verts.size(); ++i)
			{
				std::memcpy(&words[i * c_ProceduralVtxWords], &verts[i], c_ProceduralStride);
			}
			return words;
		}

		// A meshletized primitive: the meshlets, and the two pools they index into.
		struct MeshletBuild
		{
			std::vector<idl::Meshlet> meshlets;
			std::vector<uint32_t>     vertexMap;     // meshlet-local slot -> geometry vertex
			std::vector<uint32_t>     localIndices;  // meshlet-local slots, 3 per triangle
		};

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

				meshlet.vertexCount   = static_cast<uint16_t>(localVertexCount);
				meshlet.triangleCount = static_cast<uint16_t>(localTriangleCount);

				auto minBound = glm::vec3(std::numeric_limits<float>::max());
				auto maxBound = glm::vec3(std::numeric_limits<float>::lowest());
				for (const auto& [geomVertexIdx, localIdx] : localRemap)
				{
					minBound = glm::min(minBound, verts[geomVertexIdx].pos);
					maxBound = glm::max(maxBound, verts[geomVertexIdx].pos);
				}
				meshlet.boundingCenter = (minBound + maxBound) * 0.5f;
				meshlet.boundingRadius = glm::distance(maxBound, meshlet.boundingCenter);

				build.meshlets.push_back(meshlet);
			}

			return build;
		}

		std::atomic<uint32_t> g_NextSceneId{ 0 };

		idl::VertexLayout
		ConvertLayout(const assetlib::VertexLayout& src)
		{
			auto dst           = idl::VertexLayout();
			dst.attributeCount = src.attributeCount;
			dst.stride         = src.stride;
			for (uint32_t i = 0; i < src.attributeCount; ++i)
			{
				dst.attributes[i].semantic =
					static_cast<idl::VertexSemantic>(src.attributes[i].semantic);
				dst.attributes[i].format = static_cast<idl::VertexFormat>(src.attributes[i].format);
				dst.attributes[i].byteOffset = src.attributes[i].offset;
			}
			return dst;
		}

		/**
		 * Hands back the ranges of a geom that failed to build.
		 *
		 * Every Add() is registered here and released on the way out, unless Commit() says the geom was
		 * built and now owns them.
		 */
		class GeomRollback
		{
		public:
			GeomRollback() = default;

			GeomRollback(const GeomRollback&) = delete;
			GeomRollback&
			operator=(const GeomRollback&) = delete;

			// Passes `handle` straight back, so an Add() can be wrapped where it stands.
			template <typename Buffer>
			core::multi_slot_handle
			Track(Buffer& buffer, core::multi_slot_handle handle)
			{
				m_Undo.emplace_back([&buffer, handle]() { buffer.Erase(handle); });
				return handle;
			}

			void
			Commit() noexcept
			{
				m_Undo.clear();
			}

			~GeomRollback()
			{
				// Newest first, so no range is freed before one allocated after it.
				for (auto undo = m_Undo.rbegin(); undo != m_Undo.rend(); ++undo)
				{
					try
					{
						(*undo)();
					}
					catch (...)
					{
						// Already unwinding the failure that matters; a failed undo must not replace it.
					}
				}
			}

		private:
			std::vector<std::function<void()>> m_Undo;
		};
	}

	Scene::Scene(SceneDesc desc, core::SharedRef<IResourceManager> resourceManager) :
		m_Desc(std::move(desc)), m_ResourceManager(std::move(resourceManager))
	{
		m_NamePrefix = std::format("s{}:", g_NextSceneId.fetch_add(1));

		const auto atLeastOne = [](uint32_t n) -> uint32_t { return n != 0 ? n : 1; };

		const uint32_t maxSubmeshes =
			m_Desc.maxSubmeshes != 0 ? m_Desc.maxSubmeshes : m_Desc.maxMeshlets;

		// The vertex data buffer is a StructuredBuffer<uint>, so the byte budget is
		// rounded up to whole 4-byte words.
		const uint32_t maxVertexWords = (m_Desc.maxVertexBufferByteSize + 3u) / 4u;

		m_GeomSubmeshes.reset(m_Desc.maxGeom);

		{
			auto submeshBufferDesc      = RangeBufferDesc();
			submeshBufferDesc.maxCount  = atLeastOne(maxSubmeshes);
			submeshBufferDesc.debugName = "Submesh Buffer";

			m_SubmeshBuffer.Init(std::move(submeshBufferDesc), m_ResourceManager);
		}

		{
			auto meshletBufferDesc      = RangeBufferDesc();
			meshletBufferDesc.maxCount  = atLeastOne(m_Desc.maxMeshlets);
			meshletBufferDesc.debugName = "Meshlet Buffer";

			m_MeshletBuffer.Init(std::move(meshletBufferDesc), m_ResourceManager);
		}

		{
			auto vertexMapBufferDesc      = RangeBufferDesc();
			vertexMapBufferDesc.maxCount  = atLeastOne(m_Desc.maxIndices);
			vertexMapBufferDesc.debugName = "Vertex Map Buffer";

			m_VertexMapBuffer.Init(std::move(vertexMapBufferDesc), m_ResourceManager);
		}

		{
			auto vertexDataBufferDesc      = RangeBufferDesc();
			vertexDataBufferDesc.maxCount  = atLeastOne(maxVertexWords);
			vertexDataBufferDesc.debugName = "Vertex Data Buffer";

			m_VertexDataBuffer.Init(std::move(vertexDataBufferDesc), m_ResourceManager);
		}

		{
			auto indexBufferDesc      = RangeBufferDesc();
			indexBufferDesc.maxCount  = atLeastOne(m_Desc.maxIndices);
			indexBufferDesc.debugName = "Index Buffer";

			m_IndexBuffer.Init(std::move(indexBufferDesc), m_ResourceManager);
		}

		{
			auto pbrBufferDesc      = EntryBufferDesc();
			pbrBufferDesc.maxCount  = atLeastOne(m_Desc.maxPbrMaterials);
			pbrBufferDesc.debugName = "Pbr Material Buffer";

			m_Pbr.Init(std::move(pbrBufferDesc), m_ResourceManager);
		}

		{
			auto looseBufferDesc      = EntryBufferDesc();
			looseBufferDesc.maxCount  = atLeastOne(m_Desc.maxLoosePbrMaterials);
			looseBufferDesc.debugName = "Loose Pbr Material Buffer";

			m_Loose.Init(std::move(looseBufferDesc), m_ResourceManager);
		}

		m_Samplers[static_cast<size_t>(StandardSampler::kAnisoLinearWrap)] =
			m_ResourceManager->CreateSampler(
				SamplerDesc().SetAllFilters(true).SetMaxAnisotropy(16.f).SetAllAddressModes(
					SamplerAddressMode::kWrap));

		m_Samplers[static_cast<size_t>(StandardSampler::kLinearClamp)] =
			m_ResourceManager->CreateSampler(
				SamplerDesc().SetAllFilters(true).SetAllAddressModes(SamplerAddressMode::kClamp));

		// Default material textures: white (base color / ORM -> ao=1, factors drive
		// roughness+metal) and a flat tangent-space normal (0.5,0.5,1).
		m_DefaultTextures[static_cast<size_t>(DefaultTexture::kWhite)] =
			m_ResourceManager->CreateSolidTexture(255, 255, 255, 255);
		m_DefaultTextures[static_cast<size_t>(DefaultTexture::kFlatNormal)] =
			m_ResourceManager->CreateSolidTexture(128, 128, 255, 255);
	}

	void
	Scene::Update(ICommandList* cmdList)
	{
		auto buffers = GetBuffers();
		std::apply(
			[cmdList](auto&... buffer) {
				(..., (buffer.IsInitialized() ? buffer.Update(cmdList) : void()));
			},
			buffers);

		// Flush any textures loaded since the last frame (materials, environment maps).
		m_ResourceManager->FlushPendingTextureUploads(cmdList);
	}

	void
	Scene::AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx)
	{
		std::vector<std::string> updateBuffers;
		ImportResources(fg, updateBuffers);

		PassDesc desc;
		desc.SetName(std::format("Scene Update {}", drawIdx));

		for (const std::string& buffer : updateBuffers)
		{
			desc.AddBufferArg(
				BufferArg{ buffer, BarrierSyncFlag::kCopy, BarrierAccessFlag::kCopyDest });
		}

		desc.SetExec([this](const PassContext& ctx) { Update(ctx.GetCommandList()); });

		fg.AddPass(std::move(desc));
	}

	void
	Scene::ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames)
	{
		resourceNames.reserve(resourceNames.size() + c_BufferInfo.size());

		auto   buffers = GetBuffers();
		size_t i       = 0;
		std::apply(
			[&](auto&... buffer) {
				(..., [&] {
					// Import every buffer (including the GPU-only compute buffer): the
					// Update pass declares them as copy-dest so the graph transitions
					// them, and the FrameGraph tracks the state each is left in.
					std::string name(c_BufferInfo[i++].name);
					fg.ImportBuffer(name, buffer.GetBufferHandle());
					resourceNames.push_back(std::move(name));
				}());
			},
			buffers);
	}

	GeomHandle
	Scene::AddProceduralGeom(
		std::span<const VertexGen> verts,
		std::span<const uint32_t>  indices,
		MaterialHandle             material)
	{
		const auto build = BuildMeshlets(verts, indices);

		// One DispatchMesh can launch at most this many thread groups, and a procedural primitive is
		// one submesh, so its meshlets all have to fit in a single dispatch.
		if (build.meshlets.size() > c_MaxDispatchMeshGroups)
		{
			throw SceneError(
				"Scene::AddProceduralGeom: the primitive needs " +
				std::to_string(build.meshlets.size()) + " meshlets, over the " +
				std::to_string(c_MaxDispatchMeshGroups) + " a single dispatch can launch");
		}

		try
		{
			const auto vertexWords = PackVertices(verts);

			// Nothing below is the scene's until Commit(); see GeomRollback. The fallback sphere the
			// editor shows after a failed load goes through here, so a leak here is what would take
			// the fallback down too.
			auto rollback = GeomRollback();

			const auto baseVertexGlobal =
				rollback.Track(m_VertexDataBuffer, m_VertexDataBuffer.Add(vertexWords));
			const auto baseMapGlobal =
				rollback.Track(m_VertexMapBuffer, m_VertexMapBuffer.Add(build.vertexMap));
			const auto baseIndexGlobal =
				rollback.Track(m_IndexBuffer, m_IndexBuffer.Add(build.localIndices));
			const auto baseMeshletGlobal =
				rollback.Track(m_MeshletBuffer, m_MeshletBuffer.Add(build.meshlets));

			auto submesh        = idl::Submesh();
			submesh.layout      = MakeProceduralLayout();
			submesh.meshlets    = baseMeshletGlobal;
			submesh.vertexMap   = baseMapGlobal;
			submesh.vertexData  = baseVertexGlobal;
			submesh.indices     = baseIndexGlobal;
			submesh.vertexCount = static_cast<uint32_t>(verts.size());

			const auto submeshSpan = std::span<const idl::Submesh>(&submesh, 1);
			const auto baseSubmeshGlobal =
				rollback.Track(m_SubmeshBuffer, m_SubmeshBuffer.Add(submeshSpan));

			m_SubmeshBuffer.MetaAt(baseSubmeshGlobal.index) = SubmeshDefaults{ material };

			auto submeshRange = idl::RangeWithCount();
			submeshRange      = baseSubmeshGlobal;

			auto retVal     = GeomHandle();
			retVal.handle   = m_GeomSubmeshes.allocate_and_emplace(submeshRange);
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
	Scene::AddCubeGeom(MaterialHandle material)
	{
		// 6 faces x 4 verts (24 total) so each face carries its own normal, uv
		// and tangent -- an 8-vertex cube can't express per-face attributes.
		struct FaceBasis
		{
			glm::vec3 normal;
			glm::vec3 tangent;  // +u direction; bitangent = cross(normal, tangent)
		};
		static const FaceBasis faces[6] = {
			{ { 1, 0, 0 }, { 0, 0, -1 } },   // +X
			{ { -1, 0, 0 }, { 0, 0, 1 } },   // -X
			{ { 0, 1, 0 }, { 1, 0, 0 } },    // +Y
			{ { 0, -1, 0 }, { 1, 0, 0 } },   // -Y
			{ { 0, 0, 1 }, { 1, 0, 0 } },    // +Z
			{ { 0, 0, -1 }, { -1, 0, 0 } },  // -Z
		};
		// Per-face corners in (s, t) order: BL, BR, TR, TL -- CCW from outside.
		static const glm::vec2 corners[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

		std::vector<VertexGen> cubeVertices;
		std::vector<uint32_t>  cubeIndices;
		cubeVertices.reserve(24);
		cubeIndices.reserve(36);

		for (const auto& face : faces)
		{
			const glm::vec3 up   = glm::cross(face.normal, face.tangent);
			const uint32_t  base = static_cast<uint32_t>(cubeVertices.size());

			for (const auto& c : corners)
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
				constexpr auto pi       = std::numbers::pi_v<float>;
				float          xSegment = static_cast<float>(x) / static_cast<float>(xSegments);
				float          ySegment = static_cast<float>(y) / static_cast<float>(ySegments);
				float          xPos     = std::cos(xSegment * 2.0f * pi) * std::sin(ySegment * pi);
				float          yPos     = std::cos(ySegment * pi);
				float          zPos     = std::sin(xSegment * 2.0f * pi) * std::sin(ySegment * pi);

				// Tangent follows +u (increasing longitude): d(pos)/d(xSegment),
				// normalized. bitangent = cross(normal, tangent), so w = +1.
				const float a = xSegment * 2.0f * pi;

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

	void
	Scene::RequireFitsBudget(const assetlib::BMesh& mesh, const assetlib::Mesh& meshEntry) const
	{
		uint64_t vertexBytes = 0;
		uint64_t meshlets    = 0;
		uint64_t vertexMap   = 0;
		uint64_t indices     = 0;

		for (uint32_t s = 0; s < meshEntry.submeshCount; ++s)
		{
			const assetlib::Submesh& src = mesh.submeshes[meshEntry.firstSubmesh + s];

			// Each submesh is allocated its own whole number of 4-byte words, so round up per submesh
			// rather than over the total: that is what the allocator will actually be asked for.
			const uint64_t bytes = static_cast<uint64_t>(src.vertexCount) * src.layout.stride;
			vertexBytes += (bytes + 3u) / 4u * 4u;

			meshlets += src.meshletCount;

			for (uint32_t m = 0; m < src.meshletCount; ++m)
			{
				const assetlib::Meshlet& ml = mesh.meshlets[src.firstMeshlet + m];
				vertexMap += ml.vertexCount;
				indices += static_cast<uint64_t>(ml.triangleCount) * 3u;
			}
		}

		const auto atLeastOne = [](uint32_t n) -> uint64_t { return n != 0 ? n : 1u; };

		// Mirrors how the constructor sizes the arenas -- including that maxIndices budgets the vertex
		// map and the index buffer separately, and that maxSubmeshes falls back to maxMeshlets.
		const uint64_t maxVertexBytes = atLeastOne((m_Desc.maxVertexBufferByteSize + 3u) / 4u) * 4u;
		const uint64_t maxMeshlets    = atLeastOne(m_Desc.maxMeshlets);
		const uint64_t maxIndices     = atLeastOne(m_Desc.maxIndices);
		const uint64_t maxSubmeshes =
			atLeastOne(m_Desc.maxSubmeshes != 0 ? m_Desc.maxSubmeshes : m_Desc.maxMeshlets);

		const auto require = [](uint64_t         needed,
		                        uint64_t         budget,
		                        std::string_view what,
		                        std::string_view field) {
			if (needed <= budget)
				return;

			throw SceneError(
				std::format(
					"AddStaticMesh: the mesh needs {} {}, more than the scene's entire budget of "
					"{} (SceneDesc::{}); it cannot be loaded until that budget is raised",
					needed,
					what,
					budget,
					field));
		};

		require(vertexBytes, maxVertexBytes, "bytes of vertex data", "maxVertexBufferByteSize");
		require(meshlets, maxMeshlets, "meshlets", "maxMeshlets");
		require(vertexMap, maxIndices, "meshlet vertex indices", "maxIndices");
		require(indices, maxIndices, "meshlet triangle indices", "maxIndices");
		require(meshEntry.submeshCount, maxSubmeshes, "submeshes", "maxSubmeshes");
	}

	GeomHandle
	Scene::AddStaticMesh(
		const assetlib::BMesh&          mesh,
		uint32_t                        meshIndex,
		std::span<const MaterialHandle> materials)
	{
		try
		{
			if (meshIndex >= mesh.meshes.size())
			{
				throw SceneError("AddStaticMesh: meshIndex out of range");
			}

			const assetlib::Mesh& meshEntry = mesh.meshes[meshIndex];

			for (uint32_t s = 0; s < meshEntry.submeshCount; ++s)
			{
				const assetlib::Submesh& src = mesh.submeshes[meshEntry.firstSubmesh + s];

				if (src.meshletCount == 0 || src.vertexCount == 0)
				{
					throw SceneError(std::format("AddStaticMesh: submesh {} has no geometry", s));
				}

				if (src.meshletCount > c_MaxDispatchMeshGroups)
				{
					throw SceneError(
						std::format(
							"AddStaticMesh: submesh {} has {} meshlets, more than the {} thread "
							"groups a mesh dispatch can launch",
							s,
							src.meshletCount,
							c_MaxDispatchMeshGroups));
				}
			}

			RequireFitsBudget(mesh, meshEntry);

			// One GPU submesh per source submesh, in order: callers address geometry by source
			// submesh index (that is what an asset's material slots are numbered by), so the two
			// must stay 1:1.
			std::vector<idl::Submesh> submeshes;
			submeshes.reserve(meshEntry.submeshCount);

			std::vector<MaterialHandle> defaults;
			defaults.reserve(meshEntry.submeshCount);

			// Nothing below is the scene's until Commit(); see GeomRollback.
			auto rollback = GeomRollback();

			for (uint32_t s = 0; s < meshEntry.submeshCount; ++s)
			{
				const assetlib::Submesh& src = mesh.submeshes[meshEntry.firstSubmesh + s];

				const uint64_t vertexBytes =
					static_cast<uint64_t>(src.vertexCount) * src.layout.stride;

				// The offset and length come from the file, so they are the caller's claim about the
				// buffer, not a fact about it. Trusting them would read off the end of a truncated or
				// malformed .bmesh.
				if (src.vertexByteOffset + vertexBytes > mesh.vertexData.size())
				{
					throw SceneError(
						std::format(
							"AddStaticMesh: submesh {} claims {} bytes of vertex data at offset "
							"{}, "
							"past the end of the mesh's {}-byte vertex buffer",
							s,
							vertexBytes,
							src.vertexByteOffset,
							mesh.vertexData.size()));
				}

				std::vector<uint32_t> vertexWords((vertexBytes + 3u) / 4u, 0u);
				std::memcpy(
					vertexWords.data(),
					mesh.vertexData.data() + src.vertexByteOffset,
					vertexBytes);

				const MaterialHandle material =
					src.material < materials.size() ? materials[src.material] : MaterialHandle{};

				std::vector<uint32_t>     vertexMap;
				std::vector<uint32_t>     localIndices;
				std::vector<idl::Meshlet> meshlets;
				meshlets.reserve(src.meshletCount);

				for (uint32_t m = 0; m < src.meshletCount; ++m)
				{
					const assetlib::Meshlet& ml = mesh.meshlets[src.firstMeshlet + m];

					auto out                 = idl::Meshlet();
					out.relativeVertexOffset = static_cast<uint32_t>(vertexMap.size());
					out.relativeIndexOffset  = static_cast<uint32_t>(localIndices.size());
					out.vertexCount          = static_cast<uint16_t>(ml.vertexCount);
					out.triangleCount        = static_cast<uint16_t>(ml.triangleCount);
					out.boundingCenter       = ml.boundingCenter;
					out.boundingRadius       = ml.boundingRadius;

					for (uint32_t i = 0; i < ml.vertexCount; ++i)
					{
						vertexMap.push_back(mesh.meshletVertices[ml.vertexOffset + i]);
					}

					const uint32_t indexCount = ml.triangleCount * 3u;
					for (uint32_t i = 0; i < indexCount; ++i)
					{
						localIndices.push_back(mesh.meshletTriangles[ml.triangleOffset + i]);
					}

					meshlets.push_back(out);
				}

				auto submesh     = idl::Submesh();
				submesh.layout   = ConvertLayout(src.layout);
				submesh.meshlets = rollback.Track(m_MeshletBuffer, m_MeshletBuffer.Add(meshlets));
				submesh.vertexMap =
					rollback.Track(m_VertexMapBuffer, m_VertexMapBuffer.Add(vertexMap));
				submesh.vertexData =
					rollback.Track(m_VertexDataBuffer, m_VertexDataBuffer.Add(vertexWords));
				submesh.indices = rollback.Track(m_IndexBuffer, m_IndexBuffer.Add(localIndices));
				submesh.vertexCount = src.vertexCount;

				submeshes.push_back(submesh);
				defaults.push_back(material);
			}

			const auto baseSubmeshGlobal = rollback.Track(
				m_SubmeshBuffer,
				m_SubmeshBuffer.Add(std::span<const idl::Submesh>(submeshes)));

			// Meta is keyed at the range root, so it can only be filed once the range is allocated.
			m_SubmeshBuffer.MetaAt(baseSubmeshGlobal.index) = std::move(defaults);

			// RangeWithCount is assignable from the buffer handle, but not constructible from it.
			auto submeshRange = idl::RangeWithCount();
			submeshRange      = baseSubmeshGlobal;

			auto retVal     = GeomHandle();
			retVal.handle   = m_GeomSubmeshes.allocate_and_emplace(submeshRange);
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

	idl::PbrMaterial
	Scene::BuildPbrMaterial(const PbrMaterialDesc& desc) const
	{
		const auto white = m_DefaultTextures[static_cast<size_t>(DefaultTexture::kWhite)].slot;
		const auto flatNormal =
			m_DefaultTextures[static_cast<size_t>(DefaultTexture::kFlatNormal)].slot;

		// A caller-supplied texture resolves to its bindless index; an invalid
		// (default-constructed) handle falls back to the given default texture.
		const auto resolve = [](TextureAssetHandle tex, core::slot_handle fallback) {
			const core::slot_handle slot = tex.textureSlot ? tex.textureSlot : fallback;
			return idl::TextureHandle{ slot.index };
		};

		idl::PbrMaterial material{};
		material.baseColorTexture = resolve(desc.baseColorTexture, white);
		material.normalTexture    = resolve(desc.normalTexture, flatNormal);
		material.ormTexture       = resolve(desc.ormTexture, white);
		material.baseColorFactor  = desc.baseColorFactor;
		material.metallicFactor   = desc.metallicFactor;
		material.roughnessFactor  = desc.roughnessFactor;
		material.alphaCutoff      = desc.alphaCutoff;

		return material;
	}

	MaterialHandle
	Scene::CreatePbrMaterial(const PbrMaterialDesc& desc)
	{
		const core::slot_handle slot = m_Pbr.Add(BuildPbrMaterial(desc));
		return MaterialHandle{ MaterialType::kPBR, desc.layerType, slot };
	}

	void
	Scene::UpdatePbrMaterial(MaterialHandle material, const PbrMaterialDesc& desc)
	{
		if (material.materialType != MaterialType::kPBR)
		{
			throw SceneError("MaterialHandle passed to UpdatePbrMaterial is not a kPBR material");
		}
		if (!m_Pbr.IsValid(material.handle))
		{
			throw SceneError(
				"MaterialHandle passed to UpdatePbrMaterial has expired or is invalid");
		}

		// Rewriting the entry is all it takes: a submesh stores the material's entry *index*, so every
		// submesh bound to this material picks the new contents up with no rebinding. The entry keeps
		// its slot, so caller-held handles stay valid, and the PSO bucket -- which derives from
		// materialType, not from the desc -- cannot change.
		m_Pbr.Set(material.handle, BuildPbrMaterial(desc));
	}

	idl::LoosePbrMaterial
	Scene::BuildLoosePbrMaterial(const LoosePbrMaterialDesc& desc) const
	{
		const auto white = m_DefaultTextures[static_cast<size_t>(DefaultTexture::kWhite)].slot;
		const auto flatNormal =
			m_DefaultTextures[static_cast<size_t>(DefaultTexture::kFlatNormal)].slot;

		// A routed channel resolves to (its texture's bindless index, its channel). An unrouted
		// channel falls back to a default texture + channel chosen so the sampled value matches the
		// PbrMaterial default for that output: white (1.0) for base color / ORM, and the flat-normal
		// texture (R,G = 0.5) for normal X / Y.
		const auto resolve = [](const ChannelRouteDesc& route,
		                        core::slot_handle       fallbackTex,
		                        uint16_t                fallbackChannel) {
			idl::ChannelSource cs{};
			if (route.texture.textureSlot)
			{
				cs.texture = idl::TextureHandle{ route.texture.textureSlot.index };
				cs.channel = route.channel;
			}
			else
			{
				cs.texture = idl::TextureHandle{ fallbackTex.index };
				cs.channel = fallbackChannel;
			}
			return cs;
		};

		// The GPU's channel order is generated from the IDL; the file's is declared in BMaterial.h. They
		// describe the same nine routes, so a mismatch would silently sample the wrong map -- roughness
		// read as metallic, say. Pin them together rather than trusting two lists to stay in step.
		static_assert(
			idl::cLooseChannelCount == assetlib::c_LooseChannelCount,
			"The GPU and the .bmaterial file must agree on how many loose channels there are");
		static_assert(
			static_cast<size_t>(idl::PbrChannel::kBaseColorR) ==
					assetlib::ChannelIndex(assetlib::PbrChannel::kBaseColorR) &&
				static_cast<size_t>(idl::PbrChannel::kAo) ==
					assetlib::ChannelIndex(assetlib::PbrChannel::kAo) &&
				static_cast<size_t>(idl::PbrChannel::kNormalX) ==
					assetlib::ChannelIndex(assetlib::PbrChannel::kNormalX),
			"idl::PbrChannel and assetlib::PbrChannel must index BMaterial::routes identically");

		idl::LoosePbrMaterial material{};
		// Base color R,G,B,A -> white (any channel samples 1.0).
		material.sources[static_cast<size_t>(idl::PbrChannel::kBaseColorR)] =
			resolve(desc.baseColor[0], white, 0);
		material.sources[static_cast<size_t>(idl::PbrChannel::kBaseColorG)] =
			resolve(desc.baseColor[1], white, 0);
		material.sources[static_cast<size_t>(idl::PbrChannel::kBaseColorB)] =
			resolve(desc.baseColor[2], white, 0);
		material.sources[static_cast<size_t>(idl::PbrChannel::kBaseColorA)] =
			resolve(desc.baseColor[3], white, 0);
		// ORM ao,roughness,metallic -> white (1.0; factors drive rough/metal).
		material.sources[static_cast<size_t>(idl::PbrChannel::kAo)] =
			resolve(desc.orm[0], white, 0);
		material.sources[static_cast<size_t>(idl::PbrChannel::kRoughness)] =
			resolve(desc.orm[1], white, 0);
		material.sources[static_cast<size_t>(idl::PbrChannel::kMetallic)] =
			resolve(desc.orm[2], white, 0);
		// Normal X,Y -> flat-normal texture (R = 0.5, G = 0.5) -> decoded (0,0,1).
		material.sources[static_cast<size_t>(idl::PbrChannel::kNormalX)] =
			resolve(desc.normal[0], flatNormal, 0);
		material.sources[static_cast<size_t>(idl::PbrChannel::kNormalY)] =
			resolve(desc.normal[1], flatNormal, 1);

		material.baseColorFactor = desc.baseColorFactor;
		material.metallicFactor  = desc.metallicFactor;
		material.roughnessFactor = desc.roughnessFactor;
		material.alphaCutoff     = desc.alphaCutoff;

		return material;
	}

	MaterialHandle
	Scene::CreateLoosePbrMaterial(const LoosePbrMaterialDesc& desc)
	{
		const core::slot_handle slot = m_Loose.Add(BuildLoosePbrMaterial(desc));
		return MaterialHandle{ MaterialType::kLoosePbr, desc.layerType, slot };
	}

	void
	Scene::UpdateLoosePbrMaterial(MaterialHandle material, const LoosePbrMaterialDesc& desc)
	{
		if (material.materialType != MaterialType::kLoosePbr)
		{
			throw SceneError(
				"MaterialHandle passed to UpdateLoosePbrMaterial is not a kLoosePbr material");
		}
		if (!m_Loose.IsValid(material.handle))
		{
			throw SceneError(
				"MaterialHandle passed to UpdateLoosePbrMaterial has expired or is invalid");
		}

		// See UpdatePbrMaterial: the entry is rewritten in place, so every submesh bound to this
		// material follows it and the handle stays valid.
		m_Loose.Set(material.handle, BuildLoosePbrMaterial(desc));
	}

	void
	Scene::DeleteMaterial(MaterialHandle material)
	{
		// Only the two material kinds the scene allocates storage for can be freed. kNull and
		// kAssert name shading behaviour, not an entry in a buffer, so there is nothing to release.
		switch (material.materialType)
		{
		case MaterialType::kPBR:
			if (!m_Pbr.IsValid(material.handle))
			{
				throw SceneError(
					"MaterialHandle passed to DeleteMaterial has expired or is invalid");
			}
			m_Pbr.Erase(material.handle);
			return;

		case MaterialType::kLoosePbr:
			if (!m_Loose.IsValid(material.handle))
			{
				throw SceneError(
					"MaterialHandle passed to DeleteMaterial has expired or is invalid");
			}
			m_Loose.Erase(material.handle);
			return;

		case MaterialType::kInvalid:
		case MaterialType::kNull:
		case MaterialType::kAssert:
		case MaterialType::kCount:
			break;
		}

		throw SceneError("MaterialHandle passed to DeleteMaterial has no material storage");
	}

	void
	Scene::SetSubmeshMaterial(GeomHandle geom, uint32_t submeshIndex, MaterialHandle material)
	{
		if (geom.geomType != GeomType::kStaticMesh)
		{
			throw SceneError("GeomHandle passed to SetSubmeshMaterial must be of type kStaticMesh");
		}
		if (!IsGeomAlive(geom))
		{
			throw SceneError("GeomHandle passed to SetSubmeshMaterial has expired or is invalid");
		}
		if (!material.IsValid())
		{
			throw SceneError("Invalid MaterialHandle passed to SetSubmeshMaterial");
		}

		const idl::RangeWithCount& submeshes = m_GeomSubmeshes[geom.handle.index];
		if (submeshIndex >= submeshes.count)
		{
			throw SceneError("submeshIndex passed to SetSubmeshMaterial is out of range");
		}

		// Nothing is uploaded: the epoch is what carries this to instances already placed.
		m_SubmeshBuffer.MetaAt(submeshes.range.offsetStart)[submeshIndex] = material;
		++m_MaterialEpoch;
	}

	void
	Scene::DeleteGeom(GeomHandle geom)
	{
		if (geom.geomType != GeomType::kStaticMesh)
		{
			throw SceneError("GeomHandle passed to DeleteGeom must be of type kStaticMesh");
		}

		if (!IsGeomAlive(geom))
		{
			throw SceneError("GeomHandle passed to DeleteGeom refers to a deleted or unknown geom");
		}

		const auto& submeshes = m_GeomSubmeshes[geom.handle.index];

		// The geometry's per-part ranges live on each Submesh, and each submesh owns its own, so
		// free them per submesh before releasing the submesh range itself.
		const uint32_t submeshRoot = submeshes.range.offsetStart;

		for (uint32_t i = 0; i < submeshes.count; ++i)
		{
			const auto& submesh = m_SubmeshBuffer.AtIndex(submeshRoot + i);

			m_VertexDataBuffer.EraseByIndex(submesh.vertexData.offsetStart);
			m_VertexMapBuffer.EraseByIndex(submesh.vertexMap.offsetStart);
			m_IndexBuffer.EraseByIndex(submesh.indices.offsetStart);
			m_MeshletBuffer.EraseByIndex(submesh.meshlets.range.offsetStart);
		}

		m_SubmeshBuffer.EraseByIndex(submeshRoot);
		m_GeomSubmeshes.release_slot(geom.handle.index);
	}

	TextureAssetHandle
	Scene::AddTextureAsset(assetlib::ImageData img, std::string debugName)
	{
		const auto gpuTexture = m_ResourceManager->CreateTexture(img, std::move(debugName));
		return static_cast<TextureAssetHandle>(gpuTexture);
	}

	void
	Scene::DeleteTextureAsset(TextureAssetHandle texture)
	{
		// Destroying retires the slot at once, so a texture already deleted fails this check even
		// while the GPU is still finishing with it. There is nothing to remember here.
		const TextureHandle handle = TextureHandle::From(texture);
		if (handle.IsNull() || !m_ResourceManager->ValidTextureHandle(handle))
		{
			throw SceneError(
				"TextureAssetHandle passed to DeleteTextureAsset has expired or is invalid");
		}

		// Frames already submitted may still sample this texture, so only the *release* is deferred:
		// the resource manager recycles the bindless slot no earlier than the last frame that could
		// read it.
		m_ResourceManager->DestroyTexture(handle);
	}
}
