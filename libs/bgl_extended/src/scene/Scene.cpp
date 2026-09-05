#include "scene/Scene.h"
#include "cmd/CommandList.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "resource/Sampler.h"
#include "scene/NamedBuffer.h"
#include "scene/scene_buffer_names.h"
#include "types/Barrier.h"
#include "types/SubmeshInstance.h"
#include "types/VertexGen.h"
#include "types/vk_format.h"
#include "uniforms/DescriptorHandle.h"
#include "uniforms/Uniforms.h"
#include "util/util.h"
#include <RawEntry.h>
#include <algorithm>
#include <array>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMaterial.h>  // the channel layout the static_asserts below pin us to
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/VertexLayout.h>
#include <atomic>
#include <bgl/GeomHandle.h>
#include <bgl/GeomType.h>
#include <bgl/IScene.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/RigHandle.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/types/ChannelRouteDesc.h>
#include <bgl/types/FootPlantDesc.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl/types/LoosePbrMaterialDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl_common/gassert.h>
#include <bgl_common/idl/BoneSample.h>
#include <bgl_common/idl/Clip.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/LoosePbrMaterial.h>
#include <bgl_common/idl/Meshlet.h>
#include <bgl_common/idl/PbrMaterial.h>
#include <bgl_common/idl/PsoType.h>
#include <bgl_common/idl/RawRange.h>
#include <bgl_common/idl/RawTextureHandle.h>
#include <bgl_common/idl/SkinnedBone.h>
#include <bgl_common/idl/SkinnedLegChain.h>
#include <bgl_common/idl/VertexLayout.h>
#include <cmath>
#include <core/containers/multi_slot_handle.h>
#include <core/containers/slot_handle.h>
#include <core/err/util.h>
#include <core/math.h>
#include <core/ref/SharedRef.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tracy/Tracy.hpp>

#include <numbers>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		constexpr uint32_t c_MaxDispatchMeshGroups = 65535;

		// bgl links assetlib_structs, not assetlib, so findAttribute() is out of reach here -- the
		// layout is a small fixed array and this is the whole of what the check needs.
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

		std::atomic<uint32_t> g_NextSceneId{ 0 };

		// A texture handle as the bytes a raw-loaded payload stores. The descriptor exposes no
		// accessor, so the conversion is a copy -- kept in one place, as DescriptorHandle keeps its
		// own inverse.
		idl::RawTextureHandle
		RawHandleOf(DescriptorHandle descriptor) noexcept
		{
			auto raw = idl::RawTextureHandle();
			std::memcpy(&raw, &descriptor, sizeof(raw));
			return raw;
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

			// The raw arena's counterpart: it hands back a byte offset rather than a slot handle,
			// and frees by the same.
			template <typename Buffer>
			idl::RawRange
			Track(Buffer& buffer, idl::RawRange range)
			{
				m_Undo.emplace_back([&buffer, range]() { buffer.Erase(range.byteStart); });
				return range;
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
		m_Desc(std::move(desc)), m_ResourceManager(std::move(resourceManager)),
		m_Textures(m_ResourceManager)
	{
		m_NamePrefix = std::format("s{}:", g_NextSceneId.fetch_add(1));

		try
		{
			InitBuffers();
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}

		m_Samplers[static_cast<size_t>(StandardSampler::kAnisoLinearWrap)] =
			m_ResourceManager->CreateSampler(
				SamplerDesc().SetAllFilters(true).SetMaxAnisotropy(16.f).SetAllAddressModes(
					SamplerAddressMode::kWrap));

		m_Samplers[static_cast<size_t>(StandardSampler::kLinearClamp)] =
			m_ResourceManager->CreateSampler(
				SamplerDesc().SetAllFilters(true).SetAllAddressModes(SamplerAddressMode::kClamp));
	}

	void
	Scene::InitBuffers()
	{
		const auto atLeastOne = [](uint32_t n) -> uint32_t { return n != 0 ? n : 1; };

		const uint32_t initialSubmeshes =
			m_Desc.initialSubmeshes != 0 ? m_Desc.initialSubmeshes : m_Desc.initialMeshlets;

		m_Geoms.reset(atLeastOne(m_Desc.initialGeom));

		{
			auto submeshBufferDesc         = RangeBufferDesc();
			submeshBufferDesc.initialCount = atLeastOne(initialSubmeshes);
			submeshBufferDesc.debugName    = "Submesh Buffer";

			m_SubmeshBuffer.Init(std::move(submeshBufferDesc), m_ResourceManager);
		}

		{
			auto meshletBufferDesc         = RangeBufferDesc();
			meshletBufferDesc.initialCount = atLeastOne(m_Desc.initialMeshlets);
			meshletBufferDesc.debugName    = "Meshlet Buffer";

			m_MeshletBuffer.Init(std::move(meshletBufferDesc), m_ResourceManager);
		}

		{
			auto vertexMapBufferDesc         = RangeBufferDesc();
			vertexMapBufferDesc.initialCount = atLeastOne(m_Desc.initialIndices);
			vertexMapBufferDesc.debugName    = "Vertex Map Buffer";

			m_VertexMapBuffer.Init(std::move(vertexMapBufferDesc), m_ResourceManager);
		}

		{
			// Ranges alone: a vertex stream's kind is its submesh's VertexLayout, recorded once per
			// submesh rather than once per vertex, so no record here carries a header.
			auto vertexDataBufferDesc         = RawBufferDesc();
			vertexDataBufferDesc.initialBytes = atLeastOne(m_Desc.initialVertexBufferByteSize);
			vertexDataBufferDesc.debugName    = "Vertex Data Buffer";

			m_VertexDataBuffer.Init(std::move(vertexDataBufferDesc), m_ResourceManager);
		}

		{
			auto indexBufferDesc         = RangeBufferDesc();
			indexBufferDesc.initialCount = atLeastOne(m_Desc.initialIndices);
			indexBufferDesc.debugName    = "Index Buffer";

			m_IndexBuffer.Init(std::move(indexBufferDesc), m_ResourceManager);
		}

		{
			// The two kinds share one arena, so their budgets add up into it.
			const uint64_t materialBytes =
				(static_cast<uint64_t>(m_Desc.initialPbrMaterials) *
			     (idl::cRawPayloadOffset + sizeof(idl::PbrMaterial))) +
				(static_cast<uint64_t>(m_Desc.initialLoosePbrMaterials) *
			     (idl::cRawPayloadOffset + sizeof(idl::LoosePbrMaterial)));

			auto materialDesc = RawBufferDesc();

			// Clamped, not truncated: a budget past what a raw view addresses would otherwise wrap
			// to a small arena, which is the wrap the arena's own checks exist to make loud.
			materialDesc.initialBytes = atLeastOne(
				static_cast<uint32_t>(std::min<uint64_t>(materialBytes, c_MaxRawBufferBytes - 1)));
			materialDesc.debugName = "Material Arena";

			// A material payload keeps its texture handles inline, so the arena carries the typed
			// view that makes textures of them -- and re-issues it inside its own growth.
			materialDesc.handleStride = sizeof(DescriptorHandle);

			// The null record must cover the largest payload as well as its header: a null
			// reference reads zeros for a whole record rather than the first live one.
			materialDesc.nullRecordBytes =
				idl::cRawPayloadOffset +
				static_cast<uint32_t>(
					std::max(sizeof(idl::PbrMaterial), sizeof(idl::LoosePbrMaterial)));

			m_Materials.Init(std::move(materialDesc), m_ResourceManager);
		}

		// The animated buffers start at one entry each rather than from a SceneDesc knob: most scenes
		// hold no animated geometry at all, and the arenas grow on the first that does.
		{
			auto clipBufferDesc         = RangeBufferDesc();
			clipBufferDesc.initialCount = 1;
			clipBufferDesc.debugName    = "Clip Buffer";

			m_Clips.Init(std::move(clipBufferDesc), m_ResourceManager);
		}

		{
			auto rigBufferDesc         = EntryBufferDesc();
			rigBufferDesc.initialCount = 1;
			rigBufferDesc.debugName    = "Rig Buffer";

			m_Rigs.Init(std::move(rigBufferDesc), m_ResourceManager);
		}

		m_BoneAnimTables.Init(m_ResourceManager);

		{
			auto skinnedBoneBufferDesc         = RangeBufferDesc();
			skinnedBoneBufferDesc.initialCount = 1;
			skinnedBoneBufferDesc.debugName    = "Skinned Bone Buffer";

			m_SkinnedBones.Init(std::move(skinnedBoneBufferDesc), m_ResourceManager);
		}

		{
			auto boneSampleBufferDesc         = RangeBufferDesc();
			boneSampleBufferDesc.initialCount = 1;
			boneSampleBufferDesc.debugName    = "Bone Sample Buffer";

			m_BoneSamples.Init(std::move(boneSampleBufferDesc), m_ResourceManager);
		}

		{
			auto skinnedLegBufferDesc         = RangeBufferDesc();
			skinnedLegBufferDesc.initialCount = 1;
			skinnedLegBufferDesc.debugName    = "Skinned Leg Buffer";

			m_SkinnedLegs.Init(std::move(skinnedLegBufferDesc), m_ResourceManager);
		}

		{
			auto plantWeightBufferDesc         = RangeBufferDesc();
			plantWeightBufferDesc.initialCount = 1;
			plantWeightBufferDesc.debugName    = "Plant Weight Buffer";

			m_PlantWeights.Init(std::move(plantWeightBufferDesc), m_ResourceManager);
		}
	}

	core::slot_handle
	Scene::AllocateGeomSlot(const GeomRecord& record)
	{
		auto slot = m_Geoms.try_allocate_and_emplace(record);
		if (slot.is_null())
		{
			m_Geoms.grow(m_Geoms.capacity() * 2);
			slot = m_Geoms.allocate_and_emplace(record);
		}

		return slot;
	}

	void
	Scene::Update(ICommandList* cmdList)
	{
		ForEachNamedBuffer(*this, c_Buffers, [cmdList](std::string_view, auto& buffer) {
			if (buffer.IsInitialized())
			{
				buffer.Update(cmdList);
			}
		});

		// Outside the tuple, so it needs saying here. No copy is recorded -- the arena discards on
		// growth -- but a growth still supersedes a device buffer, and this is what retires it.
		m_BoneAnimTables.Update(cmdList);

		// Textures loaded since the last frame (materials, environment maps) go up on this list, so
		// the upload rides the same timeline as the frames that sample it -- another context's list
		// flushing it would leave the two unordered on the GPU.
		m_Textures.Flush(cmdList);
	}

	void
	Scene::AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx)
	{
		std::vector<std::string> updateBuffers;
		ImportResources(fg, updateBuffers);

		PassDesc desc;
		desc.SetName("Scene Update {}", drawIdx);

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
		resourceNames.reserve(resourceNames.size() + std::tuple_size_v<decltype(c_Buffers)>);

		// Import every buffer (including the GPU-only compute buffer): the Update pass declares
		// them as copy-dest so the graph transitions them, and the FrameGraph tracks the state
		// each is left in.
		ForEachNamedBuffer(*this, c_Buffers, [&](std::string_view name, const auto& buffer) {
			fg.ImportBuffer(name, buffer.GetBufferHandle());
			resourceNames.emplace_back(name);
		});

		// Outside the tuple because it is not a NamedBuffer: the arena's storage is GPU-only, and a
		// growth mints a new handle, so this is re-read every frame like the view's palette.
		auto tables = std::string(c_BoneAnimTableName);
		fg.ImportBuffer(tables, m_BoneAnimTables.GetBufferHandle());
		resourceNames.push_back(std::move(tables));
	}

	GeomHandle
	Scene::AddProceduralGeom(
		std::span<const VertexGen>     verts,
		std::span<const uint32_t>      indices,
		MaterialHandle                 material,
		const std::optional<glm::vec4> boundingSphere)
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

			auto submesh        = idl::Submesh();
			submesh.layout      = MakeProceduralLayout();
			submesh.meshlets    = baseMeshletGlobal;
			submesh.vertexMap   = baseMapGlobal;
			submesh.vertexData  = baseVertexGlobal;
			submesh.indices     = baseIndexGlobal;
			submesh.vertexCount = static_cast<uint32_t>(verts.size());

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

	void
	Scene::ValidateSkinnedRig(
		const assetlib::Skeleton&     skeleton,
		const assetlib::AnimationSet& animations,
		const FootPlantDesc&          footPlant)
	{
		const size_t boneCount = skeleton.bones.size();
		if (boneCount == 0)
		{
			throw SceneError("skinned geometry: a skeleton with no bones skins nothing");
		}
		for (size_t i = 0; i < boneCount; ++i)
		{
			const uint32_t parent = skeleton.bones[i].parent;
			// A forward-pass walk reads parent[i] before it writes i, so an equal or higher parent
			// would read a transform this frame has not written -- garbage, not a wrong pose.
			if (parent != idl::cInvalidBone && parent >= i)
			{
				throw SceneError(
					"skinned geometry: bones are not topologically sorted; every parent must be a "
					"lower index than its own bone");
			}
		}

		if (animations.boneCount != boneCount)
		{
			throw SceneError(
				"skinned geometry: the clip set was cooked against a rig of a different bone "
				"count");
		}
		if (animations.clips.empty())
		{
			throw SceneError("skinned geometry: the clip table is empty; there is no pose to play");
		}

		for (const assetlib::AnimationClip& clip : animations.clips)
		{
			// The shader clamps to frameCount - 1, and on a uint a zero underflows to four billion
			// frames of out-of-bounds reads.
			if (clip.frameCount == 0)
			{
				throw SceneError("skinned geometry: a clip with no frames has no pose to sample");
			}

			// idl::Clip addresses frames, not samples, so a base that is not a whole number of
			// frames in has no representation -- and would silently truncate to the frame below.
			if (clip.firstSample % boneCount != 0)
			{
				throw SceneError(
					"skinned geometry: a clip's first sample is not on a frame boundary");
			}

			const uint64_t end = static_cast<uint64_t>(clip.firstSample) +
			                     static_cast<uint64_t>(clip.frameCount) * boneCount;
			if (end > animations.samples.size())
			{
				throw SceneError(
					"skinned geometry: a clip's frames run past the end of the sample pool");
			}
		}

		// The pose pass keeps a solve delta per chain bone in groupshared, and that array has a fixed
		// size; past it a rig would overrun it rather than plant anything.
		if (footPlant.legs.size() > idl::cMaxLegsPerRig)
		{
			throw SceneError(
				std::format(
					"skinned geometry: a rig may author at most {} legs, and this one authors {}",
					idl::cMaxLegsPerRig,
					footPlant.legs.size()));
		}

		// A leg is authored by bone name, so a refusal carrying only an index would send whoever
		// wrote the avatar back to the rig to count bones. Falls back to the index for a bone the
		// string pool has no name for.
		const auto nameOf = [&skeleton](uint32_t bone) {
			const std::string_view name = skeleton.stringPool.at(skeleton.bones[bone].nameOffset);
			return name.empty() ? std::format("bone {}", bone) : std::format("'{}'", name);
		};

		for (const FootPlantLegDesc& leg : footPlant.legs)
		{
			const std::array<uint32_t, 4> chain = { { leg.hip, leg.knee, leg.ankle, leg.toe } };
			for (const uint32_t bone : chain)
			{
				if (bone >= boneCount)
				{
					throw SceneError(
						std::format(
							"skinned geometry: a leg names bone {}, which is outside the {}-bone "
							"skeleton it was resolved against",
							bone,
							boneCount));
				}
			}

			for (size_t link = 1; link < chain.size(); ++link)
			{
				// The solve rewrites these four slots and carries their descendants rigidly, so a
				// bone between two links would keep a pose the joints above it no longer agree
				// with.
				if (skeleton.bones[chain[link]].parent != chain[link - 1])
				{
					throw SceneError(
						std::format(
							"skinned geometry: a leg's bones are not a direct chain: {} is not "
							"parented to {}",
							nameOf(chain[link]),
							nameOf(chain[link - 1])));
				}
			}

			// Judged after normalizing, for SetGround's reason: a zero normal divides to NaN and a
			// finite one large enough to overflow the length divides to zero.
			const glm::vec3 normal = leg.soleNormal / glm::length(leg.soleNormal);
			if (!core::is_finite(normal) || glm::length(normal) == 0.0f)
			{
				throw SceneError(
					std::format(
						"skinned geometry: the sole normal on the leg ending at {} must be finite "
						"and "
						"nonzero",
						nameOf(leg.ankle)));
			}
		}

		const size_t frames = animations.samples.size() / boneCount;
		if (footPlant.plantWeights.size() != frames * footPlant.legs.size())
		{
			throw SceneError(
				"skinned geometry: the plant weights are not one byte per leg for every frame in "
				"the sample pool");
		}
	}

	RigMeta*
	Scene::FindRig(RigHandle rig) noexcept
	{
		if (!rig.IsValid() || !m_Rigs.IsValid(rig.handle))
		{
			return nullptr;
		}

		return &m_Rigs.MetaAt(rig.handle.index);
	}

	RigHandle
	Scene::AddRig(
		const assetlib::Skeleton&     skeleton,
		const assetlib::AnimationSet& animations,
		const FootPlantDesc&          footPlant)
	{
		ValidateSkinnedRig(skeleton, animations, footPlant);

		const uint32_t boneCount = static_cast<uint32_t>(skeleton.bones.size());

		// Depth is derived rather than read: no container carries it, and one forward pass is
		// enough because a parent always precedes its child.
		auto     bones    = std::vector<idl::SkinnedBone>();
		uint32_t maxDepth = 0;
		bones.reserve(boneCount);
		for (const assetlib::Bone& bone : skeleton.bones)
		{
			const uint32_t depth =
				bone.parent == idl::cInvalidBone ? 0 : bones[bone.parent].depth + 1;
			maxDepth = std::max(maxDepth, depth);
			bones.push_back({ bone.inverseBind, bone.parent, depth });
		}

		auto samples = std::vector<idl::BoneSample>();
		samples.reserve(animations.samples.size());
		for (const assetlib::Transform& sample : animations.samples)
		{
			samples.push_back(
				{ glm::vec4(sample.translation, 0.0f),
			      glm::vec4(
					  sample.rotation.x,
					  sample.rotation.y,
					  sample.rotation.z,
					  sample.rotation.w),
			      glm::vec4(sample.scale, 0.0f) });
		}

		auto clips = std::vector<idl::Clip>();
		clips.reserve(animations.clips.size());
		for (const assetlib::AnimationClip& clip : animations.clips)
		{
			clips.push_back(
				{ clip.firstSample / boneCount,
			      clip.frameCount,
			      clip.sampleRate,
			      clip.loop ? 1u : 0u });
		}

		auto legs = std::vector<idl::SkinnedLegChain>();
		legs.reserve(footPlant.legs.size());
		for (const FootPlantLegDesc& leg : footPlant.legs)
		{
			legs.push_back(
				{ glm::vec4(leg.solePoint, 0.0f),
			      glm::vec4(glm::normalize(leg.soleNormal), 0.0f),
			      leg.hip,
			      leg.knee,
			      leg.ankle,
			      leg.toe });
		}

		// Four bytes to a uint, least-significant first. The tail of the last word is zero, which is
		// a leg that is never planted -- the same thing an absent weight would mean.
		auto weights = std::vector<uint32_t>((footPlant.plantWeights.size() + 3) / 4, 0u);
		for (size_t i = 0; i < footPlant.plantWeights.size(); ++i)
		{
			weights[i / 4] |= uint32_t(footPlant.plantWeights[i]) << (8 * (i % 4));
		}

		auto rollback = GeomRollback();

		try
		{
			auto record    = idl::Rig();
			record.bones   = rollback.Track(m_SkinnedBones, m_SkinnedBones.Add(std::span(bones)));
			record.samples = rollback.Track(m_BoneSamples, m_BoneSamples.Add(std::span(samples)));
			record.clips   = rollback.Track(m_Clips, m_Clips.Add(std::span(clips)));
			if (!legs.empty())
			{
				record.legs = rollback.Track(m_SkinnedLegs, m_SkinnedLegs.Add(std::span(legs)));
				record.plantWeights =
					rollback.Track(m_PlantWeights, m_PlantWeights.Add(std::span(weights)));
			}
			record.boneCount = boneCount;
			record.maxDepth  = maxDepth;

			const core::slot_handle entry = m_Rigs.Add(record);

			RigMeta& meta   = m_Rigs.MetaAt(entry.index);
			meta.boneCount  = boneCount;
			meta.clipCount  = static_cast<uint32_t>(clips.size());
			meta.frameCount = static_cast<uint32_t>(animations.samples.size() / boneCount);
			meta.useCount   = 0;

			rollback.Commit();
			return RigHandle{ entry };
		}
		catch (const std::runtime_error& e)
		{
			throw SceneError(e.what());
		}
	}

	void
	Scene::RequestBoneAnimTable(RigHandle rig)
	{
		RigMeta* meta = FindRig(rig);
		if (meta == nullptr)
		{
			throw SceneError(
				"RigHandle passed to RequestBoneAnimTable is null, or already deleted");
		}

		if (meta->boneAnimTable)
		{
			// Allocated already. Either it holds a pose, or it is waiting for one -- and the sweep in
			// PendingRigFills is what finds the second case, so there is nothing to record here.
			return;
		}

		const uint32_t float4s = meta->frameCount * meta->boneCount * idl::cFloat4sPerBone;

		// Reserving it is what can cost -- a growth reallocates the whole arena on the device. The
		// posing itself is the GPU's, and no timestamp query exists to measure it from here.
		ZoneScopedN("bgl reserve bone anim table");
		ZoneTextF(
			"%u bones x %u frames, %llu KiB",
			meta->boneCount,
			meta->frameCount,
			(unsigned long long)((uint64_t(float4s) * sizeof(glm::vec4)) / 1024));

		// Read before the allocation, because a growth is what discards every other rig's table and
		// the capacity is the only thing that reports one.
		const uint32_t capacityBefore = m_BoneAnimTables.Capacity();

		// The arena discards on growth, and a table is written once rather than every frame, so every
		// rig holding one has to be posed again. The offsets survive; the contents do not. Run on the
		// throwing path too: a growth that then fails to hand out the slice has already discarded.
		const auto requeueIfGrown = [&] {
			if (m_BoneAnimTables.Capacity() == capacityBefore)
			{
				return;
			}

			for (uint32_t i = 0; i < m_Rigs.Capacity(); ++i)
			{
				if (m_Rigs.IsIndexValid(i) && m_Rigs.MetaAt(i).boneAnimTable)
				{
					m_Rigs.MetaAt(i).tableFilled = false;
				}
			}
		};

		try
		{
			meta->boneAnimTable = m_BoneAnimTables.Allocate(float4s);
		}
		catch (const std::runtime_error& e)
		{
			requeueIfGrown();
			throw SceneError(e.what());
		}

		m_Rigs.MetaAt(rig.handle.index).tableFilled = false;

		auto record          = m_Rigs[rig.handle];
		record.boneAnimTable = m_Rigs.MetaAt(rig.handle.index).boneAnimTable;
		m_Rigs.Set(rig.handle, record);

		requeueIfGrown();
	}

	std::span<const Scene::RigFill>
	Scene::PendingRigFills()
	{
		m_PendingRigFills.clear();

		for (uint32_t i = 0; i < m_Rigs.Capacity(); ++i)
		{
			if (!m_Rigs.IsIndexValid(i))
			{
				continue;
			}

			const RigMeta& meta = m_Rigs.MetaAt(i);
			if (meta.boneAnimTable && !meta.tableFilled)
			{
				m_PendingRigFills.push_back(RigFill{ i, meta.frameCount });
			}
		}

		return m_PendingRigFills;
	}

	void
	Scene::MarkRigFillsRecorded() noexcept
	{
		for (const RigFill& fill : m_PendingRigFills)
		{
			if (m_Rigs.IsIndexValid(fill.rigIndex))
			{
				m_Rigs.MetaAt(fill.rigIndex).tableFilled = true;
			}
		}

		m_PendingRigFills.clear();
	}

	void
	Scene::DeleteRig(RigHandle rig)
	{
		const RigMeta* meta = FindRig(rig);
		if (meta == nullptr)
		{
			throw SceneError("RigHandle passed to DeleteRig refers to a deleted or unknown rig");
		}

		// Refused rather than permitted, unlike a texture asset: a geom left naming freed bone and
		// sample ranges poses from whatever lands in them next.
		if (meta->useCount > 0)
		{
			throw SceneError(
				"RigHandle passed to DeleteRig still has geoms skinned to it; delete them first");
		}

		if (meta->boneAnimTable)
		{
			m_BoneAnimTables.Free(meta->boneAnimTable);
		}

		const idl::Rig record = m_Rigs[rig.handle];
		m_SkinnedBones.EraseByIndex(record.bones.offsetStart);
		m_BoneSamples.EraseByIndex(record.samples.offsetStart);
		m_Clips.EraseByIndex(record.clips.range.offsetStart);
		if (!record.legs.Null())
		{
			m_SkinnedLegs.EraseByIndex(record.legs.range.offsetStart);
			m_PlantWeights.EraseByIndex(record.plantWeights.offsetStart);
		}
		m_Rigs.Erase(rig.handle);
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
					"AddSkinnedMeshGeom: every submesh needs a kPBR material -- the skinned "
					"pipeline has no unlit or loose variant");
			}
		}

		GeomHandle base = AddPreparedMesh(
			CookStaticMesh(mesh, meshIndex),
			materials,
			BoundingSphereOf(posedBounds.min, posedBounds.max));

		GeomRecord& geom = m_Geoms[base.handle.index];
		geom.rig         = rig.handle;
		geom.clipCount   = rigClipCount;
		geom.boneCount   = rigBoneCount;

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
			std::vector<std::byte>    vertexBytes;
			std::vector<uint32_t>     vertexMap;
			std::vector<uint32_t>     localIndices;
			std::vector<idl::Meshlet> meshlets;
			assetlib::VertexLayout    layout;
			uint32_t                  vertexCount    = 0;
			uint32_t                  material       = 0;
			glm::vec4                 boundingSphere = glm::vec4(0.0f);
		};

		std::vector<Submesh> submeshes;
	};

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

			if (src.meshletCount > c_MaxDispatchMeshGroups)
			{
				throw SceneError(
					std::format(
						"CookStaticMesh: submesh {} has {} meshlets, more than the {} thread "
						"groups a mesh dispatch can launch",
						s,
						src.meshletCount,
						c_MaxDispatchMeshGroups));
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

			out.vertexMap.reserve(mapCount);
			out.localIndices.reserve(indexCount);
			out.meshlets.reserve(src.meshletCount);

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
		}

		auto prepared   = PreparedStaticMesh();
		prepared.m_Impl = std::move(impl);
		return prepared;
	}

	GeomHandle
	Scene::AddStaticMeshGeom(
		const assetlib::BMesh&          mesh,
		uint32_t                        meshIndex,
		std::span<const MaterialHandle> materials)
	{
		return AddStaticMeshGeom(CookStaticMesh(mesh, meshIndex), materials);
	}

	GeomHandle
	Scene::AddStaticMeshGeom(PreparedStaticMesh mesh, std::span<const MaterialHandle> materials)
	{
		return AddPreparedMesh(std::move(mesh), materials, std::nullopt);
	}

	GeomHandle
	Scene::AddPreparedMesh(
		PreparedStaticMesh              mesh,
		std::span<const MaterialHandle> materials,
		const std::optional<glm::vec4>  sphereOverride)
	{
		try
		{
			if (mesh.m_Impl == nullptr || mesh.m_Impl->submeshes.empty())
			{
				throw SceneError(
					"AddStaticMeshGeom: the prepared mesh is empty or already consumed");
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

	idl::PbrMaterial
	Scene::BuildPbrMaterial(const PbrMaterialDesc& desc) const
	{
		const auto white = m_Textures.GetDefaultSlot(TextureAssetStore::DefaultTexture::kWhite);
		const auto flatNormal =
			m_Textures.GetDefaultSlot(TextureAssetStore::DefaultTexture::kFlatNormal);

		// A caller-supplied texture resolves to its bindless descriptor; an invalid
		// (default-constructed) handle falls back to the given default texture. The descriptor comes
		// from the resource manager, not the slot: this one is read straight out of GPU memory, so it
		// has to be whatever the backend's shader can dereference -- as bytes, since the record it
		// lands in is raw-loaded and the arena's typed view is what samples them.
		const auto resolve = [this](TextureAssetHandle tex, core::slot_handle fallback) {
			const core::slot_handle slot = tex.textureSlot ? tex.textureSlot : fallback;

			return RawHandleOf(m_Textures.GetDescriptor(slot));
		};

		idl::PbrMaterial material{};
		material.baseColorTexture   = resolve(desc.baseColorTexture, white);
		material.normalTexture      = resolve(desc.normalTexture, flatNormal);
		material.ormTexture         = resolve(desc.ormTexture, white);
		material.baseColorFactor    = desc.baseColorFactor;
		material.metallicFactor     = desc.metallicFactor;
		material.roughnessFactor    = desc.roughnessFactor;
		material.specular           = glm::vec4(desc.specularColorFactor, desc.specularFactor);
		material.transmissionFactor = desc.transmissionFactor;
		material.alphaCutoff        = desc.alphaCutoff;
		material.doubleSided        = desc.doubleSided ? 1u : 0u;

		return material;
	}

	MaterialHandle
	Scene::CreatePbrMaterial(const PbrMaterialDesc& desc)
	{
		const idl::PbrMaterial material = BuildPbrMaterial(desc);
		const idl::RawEntry    entry =
			m_Materials.AddRecord(MaterialType::kPBR, std::as_bytes(std::span(&material, 1)));

		return MaterialHandle{ MaterialType::kPBR, desc.layerType, entry.byteOffset };
	}

	void
	Scene::UpdatePbrMaterial(MaterialHandle material, const PbrMaterialDesc& desc)
	{
		if (material.materialType != MaterialType::kPBR)
		{
			throw SceneError("MaterialHandle passed to UpdatePbrMaterial is not a kPBR material");
		}
		if (!m_Materials.IsOffsetValid(material.byteOffset))
		{
			throw SceneError(
				"MaterialHandle passed to UpdatePbrMaterial has expired or is invalid");
		}

		// The record says what it is, and the size guard alone would not: a stale handle whose
		// bytes were recycled into a larger record of another kind fits inside it.
		if (m_Materials.GetTagAt(material.byteOffset) != MaterialType::kPBR)
		{
			throw SceneError(
				"MaterialHandle passed to UpdatePbrMaterial names a record of another type");
		}

		// Rewriting the payload is all it takes: a submesh stores the material's byte offset, so
		// every submesh bound to this material picks the new contents up with no rebinding. The
		// record keeps its offset and its tag, so caller-held handles stay valid, and the PSO
		// bucket -- which derives from materialType, not from the desc -- cannot change.
		const idl::PbrMaterial rebuilt = BuildPbrMaterial(desc);
		m_Materials.SetRecordPayload(
			idl::RawEntry{ material.byteOffset },
			std::as_bytes(std::span(&rebuilt, 1)));

		// Every rewrite counts, including one landing on the bytes already there: an entry is a
		// GPU-layout mirror whose padding no comparison can trust.
		++m_TemporalEpoch;
	}

	idl::LoosePbrMaterial
	Scene::BuildLoosePbrMaterial(const LoosePbrMaterialDesc& desc) const
	{
		const auto white = m_Textures.GetDefaultSlot(TextureAssetStore::DefaultTexture::kWhite);
		const auto flatNormal =
			m_Textures.GetDefaultSlot(TextureAssetStore::DefaultTexture::kFlatNormal);

		// A routed channel resolves to (its texture's bindless index, its channel). An unrouted
		// channel falls back to a default texture + channel chosen so the sampled value matches the
		// PbrMaterial default for that output: white (1.0) for base color / ORM, and the flat-normal
		// texture (R,G = 0.5) for normal X / Y.
		const auto resolve = [this](
								 idl::LoosePbrMaterial&  material,
								 idl::PbrChannel         channel,
								 const ChannelRouteDesc& route,
								 core::slot_handle       fallbackTex,
								 uint16_t                fallbackChannel) {
			const bool              routed = !route.texture.textureSlot.is_null();
			const core::slot_handle slot   = routed ? route.texture.textureSlot : fallbackTex;
			const size_t            i      = static_cast<size_t>(channel);

			material.textures[i] = RawHandleOf(m_Textures.GetDescriptor(slot));
			material.channels[i] = routed ? route.channel : fallbackChannel;
		};

		// The shader finds a payload's textures by position -- handle i at payloadOffset + i * 8 --
		// so a reordered field is a silently swapped map rather than a compile error. These are what
		// make it one. See MaterialData.slang's RecordTexture.
		static_assert(offsetof(idl::PbrMaterial, baseColorTexture) == 0);
		static_assert(offsetof(idl::PbrMaterial, normalTexture) == sizeof(idl::RawTextureHandle));
		static_assert(offsetof(idl::PbrMaterial, ormTexture) == 2 * sizeof(idl::RawTextureHandle));
		static_assert(offsetof(idl::LoosePbrMaterial, textures) == 0);
		static_assert(
			sizeof(idl::LoosePbrMaterial::textures) ==
			idl::cLooseChannelCount * sizeof(idl::RawTextureHandle));

		// The other half of that arithmetic: the payload stores RawTextureHandle while the view is
		// strided by the handle itself, and a payload offset that is not a whole number of handles
		// truncates the division into the middle of a neighbouring one.
		static_assert(sizeof(DescriptorHandle) == sizeof(idl::RawTextureHandle));
		static_assert(idl::cRawPayloadOffset % sizeof(idl::RawTextureHandle) == 0);

		// The GPU's channel order is generated from the IDL; the file's is declared in BMaterial.h. They
		// describe the same nine routes, so a mismatch would silently sample the wrong map -- roughness
		// read as metallic, say. Pin them together rather than trusting two lists to stay in step.
		static_assert(
			idl::cLooseChannelCount == assetlib::c_LooseChannelCount,
			"The GPU and the .bmaterial file must agree on how many loose channels there are");
		static_assert(
			static_cast<size_t>(idl::PbrChannel::kBaseColorR) ==
					assetlib::channelIndex(assetlib::PbrChannel::kBaseColorR) &&
				static_cast<size_t>(idl::PbrChannel::kAo) ==
					assetlib::channelIndex(assetlib::PbrChannel::kAo) &&
				static_cast<size_t>(idl::PbrChannel::kNormalX) ==
					assetlib::channelIndex(assetlib::PbrChannel::kNormalX),
			"idl::PbrChannel and assetlib::PbrChannel must index BMaterial::routes identically");

		auto material = idl::LoosePbrMaterial();
		// Base color R,G,B,A -> white (any channel samples 1.0).
		resolve(material, idl::PbrChannel::kBaseColorR, desc.baseColor[0], white, 0);
		resolve(material, idl::PbrChannel::kBaseColorG, desc.baseColor[1], white, 0);
		resolve(material, idl::PbrChannel::kBaseColorB, desc.baseColor[2], white, 0);
		resolve(material, idl::PbrChannel::kBaseColorA, desc.baseColor[3], white, 0);
		// ORM ao,roughness,metallic -> white (1.0; factors drive rough/metal).
		resolve(material, idl::PbrChannel::kAo, desc.orm[0], white, 0);
		resolve(material, idl::PbrChannel::kRoughness, desc.orm[1], white, 0);
		resolve(material, idl::PbrChannel::kMetallic, desc.orm[2], white, 0);
		// Normal X,Y -> flat-normal texture (R = 0.5, G = 0.5) -> decoded (0,0,1).
		resolve(material, idl::PbrChannel::kNormalX, desc.normal[0], flatNormal, 0);
		resolve(material, idl::PbrChannel::kNormalY, desc.normal[1], flatNormal, 1);

		material.baseColorFactor    = desc.baseColorFactor;
		material.metallicFactor     = desc.metallicFactor;
		material.roughnessFactor    = desc.roughnessFactor;
		material.specular           = glm::vec4(desc.specularColorFactor, desc.specularFactor);
		material.transmissionFactor = desc.transmissionFactor;
		material.alphaCutoff        = desc.alphaCutoff;
		material.doubleSided        = desc.doubleSided ? 1u : 0u;

		return material;
	}

	MaterialHandle
	Scene::CreateLoosePbrMaterial(const LoosePbrMaterialDesc& desc)
	{
		const idl::LoosePbrMaterial material = BuildLoosePbrMaterial(desc);
		const idl::RawEntry         entry =
			m_Materials.AddRecord(MaterialType::kLoosePbr, std::as_bytes(std::span(&material, 1)));

		return MaterialHandle{ MaterialType::kLoosePbr, desc.layerType, entry.byteOffset };
	}

	void
	Scene::UpdateLoosePbrMaterial(MaterialHandle material, const LoosePbrMaterialDesc& desc)
	{
		if (material.materialType != MaterialType::kLoosePbr)
		{
			throw SceneError(
				"MaterialHandle passed to UpdateLoosePbrMaterial is not a kLoosePbr material");
		}
		if (!m_Materials.IsOffsetValid(material.byteOffset))
		{
			throw SceneError(
				"MaterialHandle passed to UpdateLoosePbrMaterial has expired or is invalid");
		}

		// The record says what it is, and the size guard alone would not: a stale handle whose
		// bytes were recycled into a larger record of another kind fits inside it.
		if (m_Materials.GetTagAt(material.byteOffset) != MaterialType::kLoosePbr)
		{
			throw SceneError(
				"MaterialHandle passed to UpdateLoosePbrMaterial names a record of another type");
		}

		// See UpdatePbrMaterial: the payload is rewritten in place, so every submesh bound to this
		// material follows it and the handle stays valid, and every rewrite moves the shading epoch.
		const idl::LoosePbrMaterial rebuilt = BuildLoosePbrMaterial(desc);
		m_Materials.SetRecordPayload(
			idl::RawEntry{ material.byteOffset },
			std::as_bytes(std::span(&rebuilt, 1)));

		++m_TemporalEpoch;
	}

	void
	Scene::DeleteMaterial(MaterialHandle material)
	{
		// Only the two material kinds the scene allocates storage for can be freed. kNull and
		// kAssert name shading behaviour, not an entry in a buffer, so there is nothing to release.
		switch (material.materialType)
		{
		case MaterialType::kPBR:
		case MaterialType::kLoosePbr:
			if (!m_Materials.IsOffsetValid(material.byteOffset))
			{
				throw SceneError(
					"MaterialHandle passed to DeleteMaterial has expired or is invalid");
			}

			// The record says what it is, so the two kinds free the same way -- and the tag is what
			// catches a handle whose type says one thing and whose offset holds another.
			if (m_Materials.GetTagAt(material.byteOffset) != material.materialType)
			{
				throw SceneError(
					"MaterialHandle passed to DeleteMaterial names a record of another type");
			}

			m_Materials.Erase(material.byteOffset);
			++m_TemporalEpoch;
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
	Scene::SetGround(const GroundPlaneDesc& ground)
	{
		if (!core::is_finite(ground.point))
		{
			throw SceneError("SetGround: the point must be finite");
		}

		// Judged after normalising, not before: a zero normal divides to NaN, and a finite one
		// large enough to overflow the length divides to zero -- both unit-length by no reading.
		const glm::vec3 normal = ground.normal / glm::length(ground.normal);
		if (!core::is_finite(normal) || glm::length(normal) == 0.0f)
		{
			throw SceneError("SetGround: the normal must be finite and not zero");
		}

		// The pose pass asks for the height under a point, which a plane with no upward component
		// cannot answer.
		if (normal.y <= 0.0f)
		{
			throw SceneError("SetGround: the normal must point up (normal.y > 0)");
		}

		m_Ground.point  = ground.point;
		m_Ground.normal = normal;

		++m_TemporalEpoch;
	}

	void
	Scene::SetSubmeshMaterial(GeomHandle geom, uint32_t submeshIndex, MaterialHandle material)
	{
		if (geom.geomType == GeomType::kInvalid || geom.geomType == GeomType::kCount)
		{
			throw SceneError("GeomHandle passed to SetSubmeshMaterial has no valid geom type");
		}
		if (!IsGeomAlive(geom))
		{
			throw SceneError("GeomHandle passed to SetSubmeshMaterial has expired or is invalid");
		}
		if (!material.IsValid())
		{
			throw SceneError("Invalid MaterialHandle passed to SetSubmeshMaterial");
		}
		if (!AcceptsMaterial(geom.geomType, material))
		{
			throw SceneError(
				"SetSubmeshMaterial: animated geometry takes a kPBR material -- neither animated "
				"pipeline has an unlit or loose variant");
		}

		const idl::RangeWithCount& submeshes = m_Geoms[geom.handle.index].submeshes;
		if (submeshIndex >= submeshes.count)
		{
			throw SceneError("submeshIndex passed to SetSubmeshMaterial is out of range");
		}

		// Nothing is uploaded: the epoch is what carries this to instances already placed.
		m_SubmeshBuffer.MetaAt(submeshes.range.offsetStart)[submeshIndex] = material;
		++m_MaterialEpoch;
		++m_TemporalEpoch;
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
		}

		m_SubmeshBuffer.EraseByIndex(submeshRoot);
		m_Geoms.release_slot(geom.handle.index);
	}

	TextureAssetHandle
	Scene::AddTextureAsset(assetlib::ImageData img, std::string debugName)
	{
		return m_Textures.Add(std::move(img), std::move(debugName));
	}

	void
	Scene::DeleteTextureAsset(TextureAssetHandle texture)
	{
		m_Textures.Delete(texture);
		++m_TemporalEpoch;
	}
}
