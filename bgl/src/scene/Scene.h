#pragma once
#include "idl/idl.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/EntryBuffer.h"
#include "scene/PackedBuffer.h"
#include "scene/RangeBuffer.h"
#include "types/BaseInstance.h"
#include <bgl/IScene.h>

namespace bgl
{
	class ICommandList;
	class FrameGraph;

	struct GeomMeta
	{
		uint32_t refCount = 0;
	};

	class Scene : public core::RefCounter<IScene>
	{
	public:
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
		GetGeometryBuffers()
		{
			return std::tie(
				m_StaticGeom,
				m_MeshletBuffer,
				m_VertexMapBuffer,
				m_VertexBuffer,
				m_IndexBuffer);
		}

		// --- SceneView support -------------------------------------------------
		// Instances live in SceneViews; they reference this Scene's geometry and
		// inc/decrement the per-geom refcount so DeleteGeom can refuse a live geom.

		[[nodiscard]] bool
		IsGeomSlotValid(core::slot_handle handle) const noexcept
		{
			return m_StaticGeom.IsValid(handle);
		}

		[[nodiscard]] bool
		IsGeomIndexValid(uint32_t index) const noexcept
		{
			return m_StaticGeom.IsIndexValid(index);
		}

		void
		IncGeomRef(uint32_t index) noexcept
		{
			++m_StaticGeom.MetaAt(index).refCount;
		}

		void
		DecGeomRef(uint32_t index) noexcept
		{
			auto& meta = m_StaticGeom.MetaAt(index);
			gassert(meta.refCount > 0, "Geom reference count underflow");
			--meta.refCount;
		}

		[[nodiscard]] const std::string&
		ResourceNamespace() const noexcept
		{
			return m_NamePrefix;
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

		EntryBuffer<idl::StaticGeom, GeomMeta> m_StaticGeom;
		RangeBuffer<idl::Meshlet>              m_MeshletBuffer;
		RangeBuffer<uint32_t>                  m_VertexMapBuffer;
		RangeBuffer<idl::Vertex>               m_VertexBuffer;
		RangeBuffer<uint32_t>                  m_IndexBuffer;

		core::SharedRef<IResourceManager> m_ResourceManager;
	};
}
