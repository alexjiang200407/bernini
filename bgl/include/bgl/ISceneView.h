#pragma once
#include <bgl/IScene.h>

namespace bgl
{
	/**
	 * A per-view set of mesh instances rendered against a shared Scene's geometry.
	 *
	 * The SceneView owns the per-view instance buffer and references the Scene whose
	 * geometry it instances. Many SceneViews can share a single Scene, so geometry
	 * (meshlets/vertices/indices) is stored once and instanced cheaply per view.
	 * Rendering takes a SceneView (see RenderContext), not a Scene.
	 */
	class BGL_API ISceneView : public core::Ref
	{
	public:
		ISceneView(ISceneView&&) noexcept      = delete;
		ISceneView(const ISceneView&) noexcept = delete;

		ISceneView&
		operator=(ISceneView&&) noexcept = delete;

		ISceneView&
		operator=(const ISceneView&) noexcept = delete;

		/**
		 * The Scene whose geometry this view instances. The view keeps it alive.
		 */
		virtual const SceneHandle&
		GetScene() const noexcept = 0;

		virtual MeshInstanceHandle
		CreateStaticMeshInstance(GeomHandle geom, MaterialHandle material, glm::mat4 transform) = 0;

		MeshInstanceHandle
		CreateStaticMeshInstance(GeomHandle geom, glm::mat4 transform)
		{
			return CreateStaticMeshInstance(
				geom,
				MaterialHandle(MaterialType::kNull, core::slot_handle()),
				transform);
		}

		/**
		 * Removes a mesh instance from this view. The geometry it referenced is left
		 * intact; the shared Scene's reference count for that geometry is decremented
		 * so the geometry can later be removed by Scene::DeleteGeom.
		 *
		 * @param instance A handle returned by CreateStaticMeshInstance.
		 * @throws SceneError if the handle is invalid or already removed.
		 */
		virtual void
		DeleteMeshInstance(MeshInstanceHandle instance) = 0;

		virtual uint32_t
		GetInstanceCount() const noexcept = 0;

	protected:
		ISceneView() noexcept = default;
	};

	using SceneViewHandle = core::SharedRef<ISceneView>;

	template class BGL_API core::SharedRef<ISceneView>;
}
