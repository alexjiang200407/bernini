#pragma once
#include <bgl/IScene.h>
#include <bgl/InstanceDesc.h>
#include <bgl/types/FootIKDesc.h>

namespace bgl
{
	struct SkyboxDesc;

	/**
	 * A per-view set of mesh instances rendered against a shared Scene's geometry.
	 *
	 * The SceneView owns the per-view instance buffer and references the Scene whose
	 * geometry it instances. Many SceneViews can share a single Scene, so geometry
	 * is stored once and instanced cheaply per view.
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
		 * The kSkinnedMesh counterpart of CreateStaticMeshInstance. Deleted through the same
		 * DeleteMeshInstance as any other placement.
		 *
		 * @throws SceneError if `geom` is not a live kSkinnedMesh geom, `desc.clip` is out of range,
		 *         or -- with `desc.source == PoseSource::kBoneAnimTable` -- the rig's bone anim table
		 *         cannot be reserved.
		 * @post With that source, the *first* such instance on a rig reserves its table:
		 *       `boneCount * frameCount` skinning matrices of device memory, tens of megabytes on a
		 *       dense rig, filled by the next frame this view is drawn. Later instances on the same
		 *       rig cost nothing.
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
		 * Rewrites the runtime foot-IK weights of a skinned instance on the per-instance source --
		 * see FootIKDesc. Written on an event and evaluated from RenderJob::time, so the pose at
		 * any clock is a function of the record: a write whose ramps all start at or after now
		 * leaves the pose the previous frame drew unchanged, which is what keeps that frame's
		 * motion vector exact. FootIKDesc::FadeTo builds such a write from GetFootIK's record.
		 *
		 * @throws SceneError if the handle is invalid or removed, the placement is not a skinned
		 *         one on the per-instance source, its rig authored no legs, or a stored leg's ramp
		 *         holds a weight outside [0, 1], a non-finite field, or an end before its start.
		 */
		virtual void
		SetFootIK(MeshInstanceHandle instance, const FootIKDesc& desc) = 0;

		/**
		 * The record SetFootIK or the spawn wrote: weight one on every leg until a write, and the
		 * default on every entry past the rig's leg count.
		 *
		 * @throws SceneError under the first three conditions SetFootIK names.
		 */
		[[nodiscard]] virtual FootIKDesc
		GetFootIK(MeshInstanceHandle instance) const = 0;

		/**
		 * Overrides the material of one submesh of ONE instance, leaving the geom's default -- and
		 * every other instance of it -- alone. This is what a cosmetic skin is: one mesh, a different
		 * material per unit. The renderer groups draws by the *resolved* material, so an opaque
		 * instance and a cutout instance of the same geom are drawn independently.
		 *
		 * The override outranks the default: a later Scene::SetSubmeshMaterial does not disturb it.
		 *
		 * Like every material binding this is a raw byte offset into the scene's material arena, so
		 * deleting a material an instance still overrides with re-points that instance at whatever
		 * record takes those bytes next. Clear the override first, or let gamelib's AssetManager
		 * refcount it.
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
