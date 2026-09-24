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
#include "uniforms/DescriptorHandle.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/ImageData.h>
#include <atomic>
#include <bgl/IScene.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/SurfaceType.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/GameSurfaceRecord.h>
#include <bgl_common/idl/Geom.h>
#include <bgl_common/idl/LoosePbrMaterial.h>
#include <bgl_common/idl/PbrMaterial.h>
#include <core/containers/slot_handle.h>
#include <core/math.h>
#include <core/ref/SharedRef.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <tuple>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		std::atomic<uint32_t> g_NextSceneId{ 0 };

		// Every surface's parameter block is a different size and one arena holds them all, so a
		// game record is budgeted by the largest surface registered rather than by its own.
		uint64_t
		SurfaceRecordBytes(std::span<const SurfaceType> surfaces) noexcept
		{
			uint32_t largestParams = 0;
			for (const SurfaceType& surface : surfaces)
				largestParams = std::max(largestParams, surface.params.byteSize);

			return idl::cRawPayloadOffset + sizeof(idl::GameSurfaceRecord) + largestParams;
		}

		// The three material kinds share one arena, so their budgets add up into it.
		uint64_t
		MaterialArenaBytes(const SceneDesc& desc, uint64_t surfaceRecordBytes) noexcept
		{
			return (static_cast<uint64_t>(desc.initialPbrMaterials) *
			        (idl::cRawPayloadOffset + sizeof(idl::PbrMaterial))) +
			       (static_cast<uint64_t>(desc.initialLoosePbrMaterials) *
			        (idl::cRawPayloadOffset + sizeof(idl::LoosePbrMaterial))) +
			       (static_cast<uint64_t>(desc.initialSurfaceMaterials) * surfaceRecordBytes);
		}

		// The null record must cover the largest payload as well as its header: a null reference
		// reads zeros for a whole record rather than the first live one.
		uint32_t
		MaterialNullRecordBytes(uint64_t surfaceRecordBytes) noexcept
		{
			return std::max(
				static_cast<uint32_t>(surfaceRecordBytes),
				idl::cRawPayloadOffset +
					static_cast<uint32_t>(
						std::max(sizeof(idl::PbrMaterial), sizeof(idl::LoosePbrMaterial))));
		}
	}

	Scene::Scene(
		SceneDesc                         desc,
		core::SharedRef<IResourceManager> resourceManager,
		std::span<const SurfaceType>      surfaces) :
		m_Desc(std::move(desc)), m_Surfaces(surfaces.begin(), surfaces.end()),
		m_ResourceManager(std::move(resourceManager)), m_Textures(m_ResourceManager)
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
			auto geomBufferDesc         = EntryBufferDesc();
			geomBufferDesc.initialCount = atLeastOne(m_Desc.initialGeom);
			geomBufferDesc.debugName    = "Geom Buffer";

			m_GeomBuffer.Init(std::move(geomBufferDesc), m_ResourceManager);
		}

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
			auto groupBufferDesc = RangeBufferDesc();
			groupBufferDesc.initialCount =
				atLeastOne(m_Desc.initialMeshlets / idl::cMeshletsPerGroup);
			groupBufferDesc.debugName = "Meshlet Group Buffer";

			m_MeshletGroupBuffer.Init(std::move(groupBufferDesc), m_ResourceManager);
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
			const uint64_t surfaceRecordBytes = SurfaceRecordBytes(m_Surfaces);
			const uint64_t materialBytes      = MaterialArenaBytes(m_Desc, surfaceRecordBytes);

			auto materialDesc = RawBufferDesc();

			// Clamped, not truncated: a budget past what a raw view addresses would otherwise wrap
			// to a small arena, which is the wrap the arena's own checks exist to make loud.
			materialDesc.initialBytes = atLeastOne(
				static_cast<uint32_t>(std::min<uint64_t>(materialBytes, c_MaxRawBufferBytes - 1)));
			materialDesc.debugName = "Material Arena";

			// A material payload keeps its texture handles inline, so the arena carries the typed
			// view that makes textures of them -- and re-issues it inside its own growth.
			materialDesc.handleStride = sizeof(DescriptorHandle);

			materialDesc.nullRecordBytes = MaterialNullRecordBytes(surfaceRecordBytes);

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

		{
			auto blendNodeBufferDesc         = RangeBufferDesc();
			blendNodeBufferDesc.initialCount = 1;
			blendNodeBufferDesc.debugName    = "Blend Node Buffer";

			m_BlendNodes.Init(std::move(blendNodeBufferDesc), m_ResourceManager);
		}

		{
			auto blendSampleBufferDesc         = RangeBufferDesc();
			blendSampleBufferDesc.initialCount = 1;
			blendSampleBufferDesc.debugName    = "Blend Sample Buffer";

			m_BlendSamples.Init(std::move(blendSampleBufferDesc), m_ResourceManager);
		}
	}

	core::slot_handle
	Scene::AllocateGeomSlot(const GeomRecord& record)
	{
		auto placed  = record;
		placed.entry = m_GeomBuffer.Add(idl::Geom{ .submeshes = record.submeshes });

		try
		{
			auto slot = m_Geoms.try_allocate_and_emplace(placed);
			if (slot.is_null())
			{
				m_Geoms.grow(m_Geoms.capacity() * 2);
				slot = m_Geoms.allocate_and_emplace(placed);
			}

			return slot;
		}
		catch (...)
		{
			m_GeomBuffer.Erase(placed.entry);
			throw;
		}
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
