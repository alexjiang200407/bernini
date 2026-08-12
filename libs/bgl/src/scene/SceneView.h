#pragma once
#include "idl/idl.h"
#include "resource/ResourceManager.h"
#include "scene/CullState.h"
#include "scene/EntryBuffer.h"
#include "scene/PackedBuffer.h"
#include "scene/TransparentSortState.h"
#include "scene/UploadBuffer.h"
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
		std::vector<uint8_t>           selected;
	};

	/**
	 * Owns a per-view instance buffer and references a shared Scene for geometry.
	 */
	class SceneView : public core::RefCounter<ISceneView>
	{
	public:
		SceneView(
			const SceneRef&                   scene,
			uint32_t                          initialInstances,
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
		SetSubmeshSelected(MeshInstanceHandle instance, uint32_t submeshIndex, bool selected)
			override;

		void
		ClearSelection() noexcept override;

		bool
		IsSubmeshSelected(MeshInstanceHandle instance, uint32_t submeshIndex) const override;

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
		GetResourceNamespace() const noexcept
		{
			return m_NamePrefix;
		}

		/**
		 * The scratch for the `cullIdx`th frustum this view is culled against. Index 0 is the
		 * camera and always exists.
		 */
		[[nodiscard]] CullState&
		GetCullState(uint32_t cullIdx) noexcept
		{
			gassert(cullIdx < m_CullStates.size(), "cull index is out of range");
			return m_CullStates[cullIdx];
		}

		[[nodiscard]] uint32_t
		GetCullStateCount() const noexcept
		{
			return static_cast<uint32_t>(m_CullStates.size());
		}

		/**
		 * Grows this view to `count` frustums, allocating the scratch each new one needs.
		 *
		 * Never shrinks. Must not be called while a frame that has already recorded against this
		 * view is in flight: a DrawData holds a raw pointer to one of these, which growing
		 * invalidates.
		 *
		 * @throws std::runtime_error if the device cannot allocate.
		 */
		void
		EnsureCullStateCount(uint32_t count);

		/** The graph namespace the `cullIdx`th frustum's outputs are imported under. */
		[[nodiscard]] std::string
		GetCullNamespace(uint32_t cullIdx) const
		{
			return std::format("{}c{}:", m_NamePrefix, cullIdx);
		}

		// The cull inputs: one per view, shared by every frustum it is culled against.
		auto
		GetInstanceBuffers()
		{
			return std::tie(m_InstanceBuffer, m_MeshBuffer);
		}

		/**
		 * The selected submesh instances as dense indices into the instance buffer -- what the
		 * selection-mask draw dispatches over. Rebuilt here if a selection change or an instance
		 * deletion left it stale, so the indices are current when read.
		 */
		[[nodiscard]] std::span<const uint32_t>
		GetSelectedInstances();

		[[nodiscard]] const UploadBuffer<uint32_t>&
		GetSelectedInstanceBuffer() const noexcept
		{
			return m_CurrentSelectedInstances;
		}

		/**
		 * Records `current` as this view's camera for frame `frameCounter` and returns the matrices it
		 * was drawn with on the previous frame -- what motion vectors reproject through.
		 *
		 * Drawing a view twice in one frame reports the same previous-frame matrices to both, rather
		 * than letting the second draw treat the first as history. The first draw a view ever takes
		 * reports its own matrices, so nothing starts life with a velocity.
		 */
		[[nodiscard]] ViewMatrices
		AdvanceCamera(uint64_t frameCounter, const ViewMatrices& current) noexcept;

		/**
		 * Whether what this view shades with -- a material's contents, a submesh's binding, the
		 * environment -- has changed since the previous call, and records this draw as having seen
		 * it. Nothing moved, so no motion vector describes such a change: a frame that sees it true
		 * cannot reproject the frames before it and has to drop them.
		 */
		[[nodiscard]] bool
		AdvanceShading() noexcept;

		void
		AttachToFrameGraph(FrameGraph& fg, uint32_t drawIdx);

		/**
		 * Imports this view's buffers under its own scope and each frustum's under one nested inside
		 * it. Leaves the graph's namespace at this view's, whatever it was on entry, since the
		 * update pass `resourceNames` feeds has to be recorded there.
		 */
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

		[[nodiscard]] const MeshMeta&
		MetaFor(MeshInstanceHandle instance, uint32_t submeshIndex, const char* what) const
		{
			return const_cast<SceneView*>(this)->MetaFor(instance, submeshIndex, what);
		}

		/** Recollects the selected dense indices; every index is re-resolved, none is cached. */
		void
		RebuildSelectedList();

		/**
		 * Sizes every per-view buffer to its starting point.
		 *
		 * @throws std::runtime_error if the device cannot allocate one; the constructor converts it
		 *         to SceneError, so a caller only ever sees the documented type.
		 */
		void
		InitBuffers();

		/**
		 * Brings the cull state back in line with the instance buffer after it has grown. A no-op
		 * when it already covers it.
		 */
		void
		SyncInstanceScratch();

		SceneRef                          m_Scene;
		Scene*                            m_SceneRaw = nullptr;
		core::SharedRef<IResourceManager> m_ResourceManager;
		std::string                       m_NamePrefix;
		uint32_t                          m_InitialInstances = 0;

		// The Scene material epoch these instances were resolved against. See Scene::MaterialEpoch.
		uint64_t m_SceneEpoch = 0;

		// This view's own shading epoch, and the sum of it and the Scene's that the last draw saw.
		// A sum, so either half moving moves it. See AdvanceShading.
		uint64_t m_ShadingEpoch      = 0;
		uint64_t m_DrawnShadingEpoch = 0;

		PackedBuffer<SubmeshInstance>    m_InstanceBuffer;
		EntryBuffer<idl::Mesh, MeshMeta> m_MeshBuffer;

		// One entry per frustum this view is culled against; index 0 is the camera.
		std::vector<CullState> m_CullStates;

		// Per view, not per frustum: only the camera sorts transparents.
		TransparentSortState m_TransparentSort;

		// The dense indices of the selected submesh instances. Any Erase on m_InstanceBuffer can
		// move a dense index, so a deletion staleness-marks the list exactly like a selection
		// change does.
		UploadBuffer<uint32_t> m_CurrentSelectedInstances;
		bool                   m_SelectionDirty = false;

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
