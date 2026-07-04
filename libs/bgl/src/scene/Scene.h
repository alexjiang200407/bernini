#pragma once
#include "idl/idl.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/EntryBuffer.h"
#include "scene/PackedBuffer.h"
#include "scene/RangeBuffer.h"
#include "types/SubmeshInstance.h"
#include <bgl/IScene.h>
#include <core/containers/slot_vector.h>

namespace bgl
{
	class ICommandList;
	class FrameGraph;

	// A geometry asset lives on the CPU only: its heavy data (submesh / vertex /
	// index / meshlet ranges) is shared in the Scene's GPU buffers, and each
	// per-placement Mesh (owned by a SceneView) copies the tiny submeshes
	// descriptor. refCount tracks live placements so DeleteGeom can refuse a
	// still-referenced asset.
	struct GeomAsset
	{
		idl::RangeWithCount submeshes;
		uint32_t            refCount = 0;
	};

	class Scene : public core::RefCounter<IScene>
	{
	public:
		enum class StandardSampler : uint32_t
		{
			kAnisoLinearWrap,
			kLinearClamp,
			kCount
		};

		Scene(SceneDesc desc, core::SharedRef<IResourceManager> resourceManager);
		~Scene() noexcept override { logger::trace("~Scene"); }
		Scene(const Scene&) noexcept = delete;
		Scene(Scene&&) noexcept      = delete;

		Scene&
		operator=(const Scene&) noexcept = delete;

		Scene&
		operator=(Scene&&) noexcept = delete;

		const SceneDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		auto
		GetBuffers()
		{
			return std::tie(
				m_SubmeshBuffer,
				m_MeshletBuffer,
				m_VertexMapBuffer,
				m_VertexDataBuffer,
				m_IndexBuffer,
				m_Pbr);
		}

		// --- SceneView support -------------------------------------------------
		// Instances live in SceneViews; they reference this Scene's geometry and
		// inc/decrement the per-geom refcount so DeleteGeom can refuse a live geom.

		[[nodiscard]] bool
		IsGeomSlotValid(core::slot_handle handle) const noexcept
		{
			return m_GeomAssets.valid(handle.index, handle.generation);
		}

		[[nodiscard]] bool
		IsGeomIndexValid(uint32_t index) const noexcept
		{
			return m_GeomAssets.allocated(index);
		}

		void
		IncGeomRef(uint32_t index) noexcept
		{
			++m_GeomAssets[index].refCount;
		}

		void
		DecGeomRef(uint32_t index) noexcept
		{
			auto& asset = m_GeomAssets[index];
			gassert(asset.refCount > 0, "Geom reference count underflow");
			--asset.refCount;
		}

		// The geometry asset (submeshes range + total meshlet count) a SceneView
		// copies into a per-placement Mesh at instance-creation time.
		[[nodiscard]] const GeomAsset&
		GetGeomAsset(uint32_t index) const noexcept
		{
			return m_GeomAssets[index];
		}

		[[nodiscard]] const std::string&
		ResourceNamespace() const noexcept
		{
			return m_NamePrefix;
		}

		[[nodiscard]] SamplerHandle
		GetSampler(StandardSampler kind) const noexcept
		{
			return m_Samplers[static_cast<size_t>(kind)];
		}

		void
		AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx);

		void
		ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames);

		void
		Update(ICommandList* cmdList);

		GeomHandle
		AddCubeGeom() override;

		GeomHandle
		AddSphereGeom(uint32_t xSegments, uint32_t ySegments, float radius) override;

		void
		DeleteGeom(GeomHandle geom) override;

	private:
		SceneDesc   m_Desc;
		std::string m_NamePrefix;

		core::slot_vector<GeomAsset> m_GeomAssets;
		RangeBuffer<idl::Submesh>    m_SubmeshBuffer;
		RangeBuffer<idl::Meshlet>    m_MeshletBuffer;
		RangeBuffer<uint32_t>        m_VertexMapBuffer;
		RangeBuffer<uint32_t>        m_VertexDataBuffer;
		RangeBuffer<uint32_t>        m_IndexBuffer;

		EntryBuffer<idl::PbrMaterial> m_Pbr;

		std::array<SamplerHandle, static_cast<size_t>(StandardSampler::kCount)> m_Samplers;

		core::SharedRef<IResourceManager> m_ResourceManager;
	};
}
