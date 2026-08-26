#pragma once
#include <bgl/IScene.h>
#include <bgl/InstanceDesc.h>

namespace bgl
{
	struct RimLightDesc;
	struct SkyboxDesc;

	/**
	 * A per-view set of mesh instances rendered against a shared Scene's geometry.
	 *
	 * The SceneView owns the per-view instance buffer and references the Scene whose
	 * geometry it instances. Many SceneViews can share a single Scene, so geometry
	 * (meshlets/vertices/indices) is stored once and instanced cheaply per view.
	 * Rendering takes a SceneView (see RenderJob), not a Scene.
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
		virtual const SceneRef&
		GetScene() const noexcept = 0;

		/**
		 * Places an instance of `geom` in this view, one drawable per submesh.
		 */
		virtual MeshInstanceHandle
		CreateStaticMeshInstance(GeomHandle geom, glm::mat4 transform) = 0;

		/**
		 * The kVatMesh counterpart of CreateStaticMeshInstance. Deleted through the same
		 * DeleteMeshInstance as any other placement.
		 *
		 * @throws SceneError if `geom` is not a live kVatMesh geom, or `desc.clip` is out of range.
		 */
		virtual MeshInstanceHandle
		CreateVatMeshInstance(
			GeomHandle             geom,
			glm::mat4              transform,
			const VatInstanceDesc& desc) = 0;

		/**
		 * The kSkinnedMesh counterpart of CreateStaticMeshInstance. Deleted through the same
		 * DeleteMeshInstance as any other placement.
		 *
		 * @throws SceneError if `geom` is not a live kSkinnedMesh geom, or `desc.clip` is out of range.
		 */
		virtual MeshInstanceHandle
		CreateSkinnedMeshInstance(
			GeomHandle                 geom,
			glm::mat4                  transform,
			const SkinnedInstanceDesc& desc) = 0;

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

		/**
		 * Overrides the material of one submesh of ONE instance, leaving the geom's default -- and
		 * every other instance of it -- alone. This is what a cosmetic skin is: one mesh, a different
		 * material per unit. The PSO follows the override, so an opaque instance and a cutout instance
		 * of the same geom draw from different pipelines.
		 *
		 * The override outranks the default: a later Scene::SetSubmeshMaterial does not disturb it.
		 *
		 * Like every material binding this is a raw slot index, so deleting a material an instance
		 * still overrides with re-points that instance at whatever takes the slot next. Clear the
		 * override first, or let gamelib's AssetManager refcount it.
		 *
		 * @throws SceneError if the instance handle is invalid, the material is invalid, or
		 *         `submeshIndex` is out of range for the instance's geometry.
		 */
		virtual void
		SetSubmeshMaterialOverride(
			MeshInstanceHandle instance,
			uint32_t           submeshIndex,
			MaterialHandle     material) = 0;

		/**
		 * Drops the override set by SetSubmeshMaterialOverride; that submesh returns to the geom's
		 * default material. A no-op on a submesh that has no override.
		 *
		 * @throws SceneError if the instance handle is invalid, or `submeshIndex` is out of range.
		 */
		virtual void
		ClearSubmeshMaterialOverride(MeshInstanceHandle instance, uint32_t submeshIndex) = 0;

		virtual uint32_t
		GetInstanceCount() const noexcept = 0;

		/**
		 * Marks one submesh of ONE instance as selected, or unmarks it. Selection is visual
		 * state for editor feedback -- the selection-outline effect draws from it -- and
		 * changes no shading or geometry. It dies with the instance: DeleteMeshInstance
		 * drops the instance's marks with it.
		 *
		 * @throws SceneError if the instance handle is invalid, or `submeshIndex` is out of
		 *         range for the instance's geometry.
		 */
		virtual void
		SetSubmeshSelected(MeshInstanceHandle instance, uint32_t submeshIndex, bool selected) = 0;

		/**
		 * Unmarks every selection in this view.
		 */
		virtual void
		ClearSelection() noexcept = 0;

		/**
		 * Whether SetSubmeshSelected has marked that submesh of that instance.
		 *
		 * @throws SceneError if the instance handle is invalid, or `submeshIndex` is out of
		 *         range for the instance's geometry.
		 */
		virtual bool
		IsSubmeshSelected(MeshInstanceHandle instance, uint32_t submeshIndex) const = 0;

		/**
		 * Binds the three precomputed IBL maps (two cubemaps + a 2D BRDF LUT) as this
		 * view's environment for the PBR pass. Replaces any previously set environment.
		 * Lighting is a per-view concern, so it lives here rather than on the shared Scene.
		 *
		 * @throws SceneError if any handle is invalid, or if the irradiance/prefilter
		 *         maps are not cube maps.
		 */
		virtual void
		SetEnvironmentMap(const EnvironmentMapDesc& desc) = 0;

		/**
		 * Binds a cubemap as this view's skybox background, drawn behind the scene.
		 * Replaces any previously set skybox.
		 *
		 * @param desc Description of the skybox.
		 * @throws SceneError if the handle is invalid or is not a cube map.
		 */
		virtual void
		SetSkyBox(SkyboxDesc desc) = 0;

		/**
		 * Sets this view's rim light, replacing any previously set one. Like the environment it is
		 * sampled from, a rim is per view.
		 *
		 * Nothing rims until an instance asks for a share of it -- see SetInstanceRimIntensity --
		 * and an intensity of 0, which is what an untouched view has, is a rim light that is off.
		 *
		 * @throws SceneError if `intensity` or `power` is not finite, or either is negative.
		 */
		virtual void
		SetRimLight(const RimLightDesc& desc) = 0;

		/**
		 * Sets how much of this view's rim light one instance catches, scaling it. Zero -- which is
		 * what a fresh instance has -- catches none: a rim is a readability device for units, and a
		 * wall or a floor that caught one would read as lit from nowhere.
		 *
		 * The view owns the rim's colour and shape; this is only the share of it, so one number
		 * changes a dusk and every instance still differs by what it was authored to catch.
		 *
		 * Per instance and not per geom, so two placements of one mesh can differ. What sets it is
		 * the mesh's authored default, which gamelib reads from the `.bimport`.
		 *
		 * @throws SceneError if the instance handle is invalid, or `rimIntensity` is not finite or
		 *         is negative.
		 */
		virtual void
		SetInstanceRimIntensity(MeshInstanceHandle instance, float rimIntensity) = 0;

		/**
		 * The share of this view's rim light that instance catches.
		 *
		 * @throws SceneError if the instance handle is invalid.
		 */
		[[nodiscard]] virtual float
		GetInstanceRimIntensity(MeshInstanceHandle instance) const = 0;

		/**
		 * Sets this view's photographic exposure: a linear scale applied to the shaded radiance just
		 * before tone mapping. Like the environment, exposure is per-view, so two views of one Scene
		 * can be exposed independently.
		 *
		 * It scales *total* radiance, not the environment's contribution -- it is the camera's
		 * sensitivity, not a property of the IBL maps.
		 *
		 * @param exposure Linear multiplier. 1.0 (the default) passes radiance through unscaled.
		 * @throws SceneError if `exposure` is not finite or is negative.
		 */
		virtual void
		SetExposure(float exposure) = 0;

	protected:
		ISceneView() noexcept = default;
	};

	using SceneViewRef = core::SharedRef<ISceneView>;
}

template class BGL_API core::SharedRef<bgl::ISceneView>;
