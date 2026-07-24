#pragma once
#include "idl/idl.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/EntryBuffer.h"
#include "scene/PackedBuffer.h"
#include "types/EnvironmentMap.h"
#include "types/SubmeshInstance.h"
#include "types/ViewMatrices.h"
#include <bgl/ISceneView.h>
#include <bgl/SkyboxDesc.h>
#include <core/ref/RefCounter.h>

namespace bgl
{
	class ICommandList;
	class FrameGraph;
	class Scene;

	struct MeshMeta
	{
		std::vector<core::slot_handle> submeshInstances;
		std::vector<MaterialHandle>    overrides;
	};

	/**
	 * Owns a per-view instance buffer and references a shared Scene for geometry.
	 */
	class SceneView : public core::RefCounter<ISceneView>
	{
	public:
		SceneView(
			const SceneRef&                   scene,
			uint32_t                          maxInstances,
			core::SharedRef<IResourceManager> resourceManager);

		~SceneView() noexcept override;

		SceneView(const SceneView&) noexcept = delete;
		SceneView(SceneView&&) noexcept      = delete;

		SceneView&
		operator=(const SceneView&) noexcept = delete;

		SceneView&
		operator=(SceneView&&) noexcept = delete;

		const SceneRef&
		GetScene() const noexcept override
		{
			return m_Scene;
		}

		MeshInstanceHandle
		CreateStaticMeshInstance(GeomHandle geom, glm::mat4 transform) override;

		void
		DeleteMeshInstance(MeshInstanceHandle instance) override;

		void
		SetSubmeshMaterialOverride(
			MeshInstanceHandle instance,
			uint32_t           submeshIndex,
			MaterialHandle     material) override;

		void
		ClearSubmeshMaterialOverride(MeshInstanceHandle instance, uint32_t submeshIndex) override;

		void
		SetEnvironmentMap(const EnvironmentMapDesc& desc) override;

		void
		SetSkyBox(SkyboxDesc desc) override;

		void
		SetExposure(float exposure) override;

		[[nodiscard]] const EnvironmentMap&
		GetEnvironmentMap() const noexcept
		{
			return m_EnvironmentMap;
		}

		[[nodiscard]] float
		GetExposure() const noexcept
		{
			return m_Exposure;
		}

		[[nodiscard]] const std::optional<SkyboxDesc>&
		GetSkybox() const noexcept
		{
			return m_Skybox;
		}

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

		/**
		 * Records `current` as this view's camera for frame `frameId` and returns the matrices it
		 * was drawn with on the previous frame -- what motion vectors reproject through.
		 *
		 * Drawing a view twice in one frame reports the same previous-frame matrices to both, rather
		 * than letting the second draw treat the first as history. The first draw a view ever takes
		 * reports its own matrices, so nothing starts life with a velocity.
		 */
		[[nodiscard]] ViewMatrices
		AdvanceCamera(uint64_t frameId, const ViewMatrices& current) noexcept;

		void
		AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx);

		void
		ImportResources(FrameGraph& fg, std::vector<std::string>& resourceNames);

		void
		Update(ICommandList* cmdList);

	private:
		/**
		 * Fills `instance`'s material + PSO: `override` if it is valid, else the Scene's default for
		 * that submesh. `submeshRoot` is where its geom's range starts; the instance names its own
		 * offset into that range.
		 *
		 * The only place a SubmeshInstance's shading is decided, so an instance that was just placed,
		 * one that was just overridden, and one re-resolved by a default change cannot disagree.
		 */
		void
		ResolveShading(
			SubmeshInstance& instance,
			uint32_t         submeshRoot,
			MaterialHandle   materialOverride) const;

		/** Re-resolves one submesh instance of `meshIndex` and uploads it if it moved. */
		void
		RefreshSubmeshInstance(uint32_t meshIndex, uint32_t submeshIndex);

		/**
		 * Re-resolves every non-overridden instance against the Scene's current defaults, rewriting
		 * only those that changed. O(instances), but only runs after a SetSubmeshMaterial -- an
		 * authoring action.
		 */
		void
		ReresolveInstances();

		/** The mesh record `instance` names, with `submeshIndex` bounds-checked against it. */
		[[nodiscard]] MeshMeta&
		MetaFor(MeshInstanceHandle instance, uint32_t submeshIndex, const char* what);

		SceneRef                          m_Scene;
		Scene*                            m_SceneRaw = nullptr;
		core::SharedRef<IResourceManager> m_ResourceManager;
		std::string                       m_NamePrefix;
		uint32_t                          m_MaxInstances = 0;

		// The Scene material epoch these instances were resolved against. See Scene::MaterialEpoch.
		uint64_t m_SceneEpoch = 0;

		PackedBuffer<SubmeshInstance>    m_InstanceBuffer;
		EntryBuffer<idl::Mesh, MeshMeta> m_MeshBuffer;
		ComputeBuffer                    m_CompactedInstances;

		// One word per instance slot, written by the cull pass and read by the counting sort and the
		// transparent depth-key pass. Sized like the instance buffer.
		ComputeBuffer m_InstanceVisibility;

		// The depth-sorted transparent path, all written by TransparentSortPass. The keys buffer is
		// sized off the instance buffer, not the sort capacity: see the note at its initialization.
		ComputeBuffer m_SortedTransparentInstances;
		ComputeBuffer m_TransparentSortEntries;
		ComputeBuffer m_TransparentSortCount;

		EnvironmentMap            m_EnvironmentMap;
		std::optional<SkyboxDesc> m_Skybox;
		float                     m_Exposure = 1.0f;

		// This view's camera now and on the frame before, plus the frame m_PrevCamera last rolled
		// over on. See AdvanceCamera.
		ViewMatrices            m_Camera;
		ViewMatrices            m_PrevCamera;
		std::optional<uint64_t> m_CameraFrame;
	};
}
