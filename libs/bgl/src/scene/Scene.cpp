#include "scene/Scene.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "scene/NamedBuffer.h"
#include "scene/scene_buffer_names.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/ImageData.h>
#include <atomic>
#include <bgl/IScene.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/SurfaceType.h>
#include <bgl/idl/Constants.h>
#include <bgl/idl/GameSurfaceRecord.h>
#include <bgl/idl/Geom.h>
#include <bgl/idl/LoosePbrMaterial.h>
#include <bgl/idl/PbrMaterial.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/TextureAssetHandle.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/idl/RawArena.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/uniforms/DescriptorHandle.h>
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

			return bgpu::idl::cRawPayloadOffset + sizeof(idl::GameSurfaceRecord) + largestParams;
		}

		// The three material kinds share one arena, so their budgets add up into it.
		uint64_t
		MaterialArenaBytes(const SceneDesc& desc, uint64_t surfaceRecordBytes) noexcept
		{
			return (static_cast<uint64_t>(desc.initialPbrMaterials) *
			        (bgpu::idl::cRawPayloadOffset + sizeof(idl::PbrMaterial))) +
			       (static_cast<uint64_t>(desc.initialLoosePbrMaterials) *
			        (bgpu::idl::cRawPayloadOffset + sizeof(idl::LoosePbrMaterial))) +
			       (static_cast<uint64_t>(desc.initialSurfaceMaterials) * surfaceRecordBytes);
		}

		// The null record must cover the largest payload as well as its header: a null reference
		// reads zeros for a whole record rather than the first live one.
		uint32_t
		MaterialNullRecordBytes(uint64_t surfaceRecordBytes) noexcept
		{
			return std::max(
				static_cast<uint32_t>(surfaceRecordBytes),
				bgpu::idl::cRawPayloadOffset +
					static_cast<uint32_t>(
						std::max(sizeof(idl::PbrMaterial), sizeof(idl::LoosePbrMaterial))));
		}

		uint32_t
		AtLeastOne(uint32_t n) noexcept
		{
			return n != 0 ? n : 1;
		}

		bgpu::RawBufferDesc
		MaterialArenaDesc(const SceneDesc& desc, std::span<const SurfaceType> surfaces)
		{
			const uint64_t surfaceRecordBytes = SurfaceRecordBytes(surfaces);
			const uint64_t materialBytes      = MaterialArenaBytes(desc, surfaceRecordBytes);

			// Clamped, not truncated: a budget past what a raw view addresses would otherwise wrap
			// to a small arena, which is the wrap the arena's own checks exist to make loud.
			return bgpu::RawBufferDesc()
			    .SetInitialBytes(AtLeastOne(
					static_cast<uint32_t>(
						std::min<uint64_t>(materialBytes, bgpu::c_MaxRawBufferBytes - 1))))
			    .SetDebugName("Material Arena")
			    // A material payload keeps its texture handles inline, so the arena carries the typed
			    // view that makes textures of them -- and re-issues it inside its own growth.
			    .SetHandleStride(sizeof(bgpu::DescriptorHandle))
			    .SetNullRecordBytes(MaterialNullRecordBytes(surfaceRecordBytes));
		}
	}

	// The animated and grass buffers start at one entry each rather than from a SceneDesc knob: most
	// scenes hold neither, and the arenas grow on the first that does.
	Scene::Scene(
		SceneDesc                               desc,
		core::SharedRef<bgpu::IResourceManager> resourceManager,
		std::span<const SurfaceType>            surfaces)
	try :
		m_Desc(std::move(desc)), m_Surfaces(surfaces.begin(), surfaces.end()),
		m_GrassLooks(
			resourceManager,
			bgpu::EntryBufferDesc().SetInitialCount(1).SetDebugName("Grass Look Buffer")),
		m_GrassChunks(
			resourceManager,
			bgpu::RangeBufferDesc().SetInitialCount(1).SetDebugName("Grass Chunk Buffer")),
		m_GrassClumps(
			resourceManager,
			bgpu::RangeBufferDesc().SetInitialCount(1).SetDebugName("Grass Clump Buffer")),
		m_GeomBuffer(
			resourceManager,
			bgpu::EntryBufferDesc()
				.SetInitialCount(AtLeastOne(m_Desc.initialGeom))
				.SetDebugName("Geom Buffer")),
		m_SubmeshBuffer(
			resourceManager,
			bgpu::RangeBufferDesc()
				.SetInitialCount(AtLeastOne(
					m_Desc.initialSubmeshes != 0 ? m_Desc.initialSubmeshes :
												   m_Desc.initialMeshlets))
				.SetDebugName("Submesh Buffer")),
		m_MeshletBuffer(
			resourceManager,
			bgpu::RangeBufferDesc()
				.SetInitialCount(AtLeastOne(m_Desc.initialMeshlets))
				.SetDebugName("Meshlet Buffer")),
		m_MeshletGroupBuffer(
			resourceManager,
			bgpu::RangeBufferDesc()
				.SetInitialCount(AtLeastOne(m_Desc.initialMeshlets / idl::cMeshletsPerGroup))
				.SetDebugName("Meshlet Group Buffer")),
		m_VertexMapBuffer(
			resourceManager,
			bgpu::RangeBufferDesc()
				.SetInitialCount(AtLeastOne(m_Desc.initialIndices))
				.SetDebugName("Vertex Map Buffer")),
		// Ranges alone: a vertex stream's kind is its submesh's VertexLayout, recorded once per
		// submesh rather than once per vertex, so no record here carries a header.
		m_VertexDataBuffer(
			resourceManager,
			bgpu::RawBufferDesc()
				.SetInitialBytes(AtLeastOne(m_Desc.initialVertexBufferByteSize))
				.SetDebugName("Vertex Data Buffer")),
		m_IndexBuffer(
			resourceManager,
			bgpu::RangeBufferDesc()
				.SetInitialCount(AtLeastOne(m_Desc.initialIndices))
				.SetDebugName("Index Buffer")),
		m_Materials(resourceManager, MaterialArenaDesc(m_Desc, m_Surfaces)),
		m_Clips(
			resourceManager,
			bgpu::RangeBufferDesc().SetInitialCount(1).SetDebugName("Clip Buffer")),
		m_Rigs(
			resourceManager,
			bgpu::EntryBufferDesc().SetInitialCount(1).SetDebugName("Rig Buffer")),
		m_SkinnedBones(
			resourceManager,
			bgpu::RangeBufferDesc().SetInitialCount(1).SetDebugName("Skinned Bone Buffer")),
		m_BoneSamples(
			resourceManager,
			bgpu::RangeBufferDesc().SetInitialCount(1).SetDebugName("Bone Sample Buffer")),
		m_BoneAnimTables(resourceManager),
		m_SkinnedLegs(
			resourceManager,
			bgpu::RangeBufferDesc().SetInitialCount(1).SetDebugName("Skinned Leg Buffer")),
		m_PlantWeights(
			resourceManager,
			bgpu::RangeBufferDesc().SetInitialCount(1).SetDebugName("Plant Weight Buffer")),
		m_BlendNodes(
			resourceManager,
			bgpu::RangeBufferDesc().SetInitialCount(1).SetDebugName("Blend Node Buffer")),
		m_BlendSamples(
			resourceManager,
			bgpu::RangeBufferDesc().SetInitialCount(1).SetDebugName("Blend Sample Buffer")),
		m_ResourceManager(std::move(resourceManager)), m_Textures(m_ResourceManager)
	{
		m_NamePrefix = std::format("s{}:", g_NextSceneId.fetch_add(1));

		m_Geoms.reset(AtLeastOne(m_Desc.initialGeom));

		m_Samplers[static_cast<size_t>(StandardSampler::kAnisoLinearWrap)] =
			m_ResourceManager->CreateSampler(
				bgpu::SamplerDesc().SetAllFilters(true).SetMaxAnisotropy(16.f).SetAllAddressModes(
					bgpu::SamplerAddressMode::kWrap));

		m_Samplers[static_cast<size_t>(StandardSampler::kLinearClamp)] =
			m_ResourceManager->CreateSampler(
				bgpu::SamplerDesc().SetAllFilters(true).SetAllAddressModes(
					bgpu::SamplerAddressMode::kClamp));
	}
	// A device that cannot allocate a buffer reaches the caller as the documented type.
	catch (const SceneError&)
	{
		throw;
	}
	catch (const std::runtime_error& e)
	{
		throw SceneError(e.what());
	}

	core::slot_handle
	Scene::AllocateGeomSlot(const GeomRecord& record)
	{
		auto placed         = record;
		auto geom           = idl::Geom();
		geom.boundingSphere = record.boundingSphere;
		geom.submeshes      = record.submeshes;
		std::ranges::copy(record.lodMinPixels, geom.lodMinPixels);
		placed.entry = m_GeomBuffer.Add(geom);

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
	Scene::Update(bgpu::ICommandList* cmdList)
	{
		ForEachNamedBuffer(*this, c_Buffers, [cmdList](std::string_view, auto& buffer) {
			buffer.Update(cmdList);
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
			desc.AddCopyDest(buffer);
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
