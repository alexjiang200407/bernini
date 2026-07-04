#pragma once
#include "idl/idl.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/EntryBuffer.h"
#include "scene/PackedBuffer.h"
#include "types/SubmeshInstance.h"
#include <bgl/ISceneView.h>
#include <core/ref/RefCounter.h>

namespace bgl
{
	class ICommandList;
	class FrameGraph;
	class Scene;

	// index (so DeleteMeshInstance / ~SceneView can decrement the right refcount; the
	// per-placement Mesh embeds the submeshes descriptor, not a geom back-ref), the
	// geom type (so SetSubmeshMaterial can recompute a submesh's PSO), and the handles
	// of this instance's submesh-instances in m_InstanceBuffer (one per submesh).
	struct MeshMeta
	{
		uint32_t                       geomIndex = 0;
		GeomType                       geomType  = GeomType::kInvalid;
		std::vector<core::slot_handle> submeshInstances;
	};

	/**
	 * Owns a per-view instance buffer and references a shared Scene for geometry.
	 * The geometry refcount lives on the Scene; this view inc/decrements it as
	 * instances are created/removed, and its destructor releases the remaining ones.
	 */
	class SceneView : public core::RefCounter<ISceneView>
	{
	public:
		SceneView(
			const SceneHandle&                scene,
			uint32_t                          maxInstances,
			core::SharedRef<IResourceManager> resourceManager);

		~SceneView() noexcept override;

		SceneView(const SceneView&) noexcept = delete;
		SceneView(SceneView&&) noexcept      = delete;

		SceneView&
		operator=(const SceneView&) noexcept = delete;

		SceneView&
		operator=(SceneView&&) noexcept = delete;

		const SceneHandle&
		GetScene() const noexcept override
		{
			return m_Scene;
		}

		MeshInstanceHandle
		CreateStaticMeshInstance(GeomHandle geom, MaterialHandle material, glm::mat4 transform)
			override;

		void
		DeleteMeshInstance(MeshInstanceHandle instance) override;

		void
		SetSubmeshMaterial(
			MeshInstanceHandle instance,
			uint32_t           submeshIndex,
			MaterialHandle     material) override;

		[[nodiscard]] uint32_t
		GetInstanceCount() const noexcept override
		{
			return m_InstanceBuffer.Size();
		}

		[[nodiscard]] const std::string&
		ResourceNamespace() const noexcept
		{
			return m_NamePrefix;
		}

		auto
		GetInstanceBuffers()
		{
			return std::tie(m_InstanceBuffer, m_MeshBuffer, m_CompactedInstances);
		}

		void
		AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx);

		void
		ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames);

		void
		Update(ICommandList* cmdList);

	private:
		SceneHandle                       m_Scene;
		Scene*                            m_SceneRaw = nullptr;
		core::SharedRef<IResourceManager> m_ResourceManager;
		std::string                       m_NamePrefix;
		uint32_t                          m_MaxInstances = 0;

		PackedBuffer<SubmeshInstance>    m_InstanceBuffer;
		EntryBuffer<idl::Mesh, MeshMeta> m_MeshBuffer;
		ComputeBuffer                    m_CompactedInstances;
	};
}
