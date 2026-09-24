#pragma once
#include <bgl/GeomHandle.h>
#include <bgl/IScene.h>
#include <bgl/InstanceDesc.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/api.h>
#include <bgl/types/BlobShadowDesc.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <bgl/types/EnvironmentMapDesc.h>
#include <bgl/types/FootIKDesc.h>
#include <bgl/types/MeshInstanceFlags.h>
#include <bgl/types/WindDesc.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <optional>

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
		 * The same placement spawned on a whole playback record rather than one clip. Always the
		 * per-instance source: a blend has nowhere to live on a pose the whole rig shares.
		 *
		 * @throws SceneError if `geom` is not a live kSkinnedMesh geom, a slot names a node past
		 *         the rig's table, a weight is negative, a ramp ends before it starts, a value is
		 *         not finite, or no slot carries any weight.
		 */
		virtual MeshInstanceHandle
		CreateSkinnedMeshInstance(
			GeomHandle                 geom,
			glm::mat4                  transform,
			const SkinnedPlaybackDesc& desc) = 0;

		/**
		 * Rewrites a per-instance skinned placement's playback record in place. The instance keeps
		 * its pose storage and its place among the instances posed each frame; only what is
		 * evaluated changes, from the next frame drawn.
		 *
		 * The caller owns the property that makes this safe under temporal reprojection: the new
		 * record evaluated at the previous frame's time must give the pose the old one did. See
		 * SkinnedPlaybackDesc.
		 *
		 * @throws SceneError if the handle is invalid or removed, the placement is not a skinned
		 *         one on the per-instance source, or `desc` fails the checks CreateSkinnedMeshInstance
		 *         makes.
		 */
		virtual void
		SetSkinnedPlayback(MeshInstanceHandle instance, const SkinnedPlaybackDesc& desc) = 0;

		/**
		 * The record SetSkinnedPlayback or the spawn wrote, so a caller need not keep a copy.
		 *
		 * @throws SceneError if the handle is invalid or removed, or the placement is not a skinned
		 *         one on the per-instance source.
		 */
		[[nodiscard]] virtual SkinnedPlaybackDesc
		GetSkinnedPlayback(MeshInstanceHandle instance) const = 0;

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
		 * Moves a placement, effective on the next frame this view is drawn. The move writes a motion
		 * vector rather than disturbing the temporal filter.
		 *
		 * The previous transform is the one the last *drawn* frame used, not the one the last write
		 * replaced: writing twice in a frame reports the same velocity as writing once, and a
		 * placement not written this frame has a velocity of exactly zero.
		 *
		 * A per-instance CPU write -- each scattered write uploads a block -- so not the path for
		 * moving a crowd every frame.
		 *
		 * @param transform An affine model-to-world matrix; its fourth row is discarded, not checked.
		 * @throws SceneError if the handle is invalid or already removed.
		 */
		virtual void
		SetInstanceTransform(MeshInstanceHandle instance, const glm::mat4& transform) = 0;

		/**
		 * The matrix the placement was created with, or the one SetInstanceTransform last wrote.
		 *
		 * @throws SceneError if the handle is invalid or already removed.
		 */
		[[nodiscard]] virtual glm::mat4
		GetInstanceTransform(MeshInstanceHandle instance) const = 0;

		/**
		 * Replaces the placement's whole flags word, effective on the next frame this view is drawn --
		 * see MeshInstanceFlag for what each bit does. A placement is created with none set.
		 *
		 * A change that alters what is drawn moves the temporal epoch, as a deletion does: a surface
		 * that appears or vanishes has no motion vector to describe it.
		 *
		 * @throws SceneError if the handle is invalid or already removed.
		 */
		virtual void
		SetMeshInstanceFlags(MeshInstanceHandle instance, MeshInstanceFlags flags) = 0;

		/**
		 * The word SetMeshInstanceFlags last wrote, or empty.
		 *
		 * @throws SceneError if the handle is invalid or already removed.
		 */
		[[nodiscard]] virtual MeshInstanceFlags
		GetMeshInstanceFlags(MeshInstanceHandle instance) const = 0;

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
		 * Whether `instance` owns a foot-IK record: a live skinned placement on the per-instance
		 * source whose rig authored legs. Exactly when SetFootIK and GetFootIK would not throw, for
		 * a caller that cannot tell a rig's legs from the outside.
		 */
		[[nodiscard]] virtual bool
		HasFootIK(MeshInstanceHandle instance) const noexcept = 0;

		/**
		 * Gives one placement a blob shadow: a soft radial-falloff disc draped over the static
		 * geometry directly beneath it, fading per pixel as the gap between the placement and the
		 * surface grows -- see BlobShadowDesc. Only static, upward-facing surfaces receive it: a
		 * unit never catches a neighbour's shadow, and a wall beside the placement keeps its face.
		 * Replaces any blob shadow the placement holds. Any placement may carry one; the expected
		 * consumers are skinned units, which nothing enforces. `desc.feet` adds a shadow under each
		 * foot, read from the pose the frame draws -- see FootShadowDesc.
		 *
		 * @throws SceneError if the handle is invalid or removed, `desc.radius` or
		 *         `desc.fadeHeight` is not finite and positive, `desc.intensity` is not
		 *         finite in [0, 1], or `desc.casterLift` is not finite and non-negative; or if
		 *         `desc.feet` is set on a placement HasFootIK refuses, or holds a field outside
		 *         the same bounds (`maxReceiverRise` as `casterLift`).
		 */
		virtual void
		SetBlobShadow(MeshInstanceHandle instance, const BlobShadowDesc& desc) = 0;

		/**
		 * Removes the placement's blob shadow. A no-op on a placement that carries none.
		 *
		 * @throws SceneError if the handle is invalid or already removed.
		 */
		virtual void
		ClearBlobShadow(MeshInstanceHandle instance) = 0;

		/**
		 * The record SetBlobShadow last wrote, or empty if the placement carries none.
		 *
		 * @throws SceneError if the handle is invalid or already removed.
		 */
		[[nodiscard]] virtual std::optional<BlobShadowDesc>
		GetBlobShadow(MeshInstanceHandle instance) const = 0;

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
		 * Sets this view's one analytic light: a sun, casting no shadow. Replaces any previously set
		 * light. Per-view for the same reason the environment is -- two views of one Scene are lit
		 * independently.
		 *
		 * It *adds* to the environment map rather than replacing it, and the environment already
		 * carries whatever sun its source HDR held, so the two double-count a sun that is in both.
		 * Which one to turn down is the caller's decision; nothing here can tell.
		 *
		 * A view that never calls this is lit by its environment alone, exactly as before this
		 * existed: the default intensity is 0.
		 *
		 * `desc.direction` need not be normalized; only a zero-length one is refused, since a sun
		 * pointing nowhere has no direction to fall back on.
		 *
		 * @throws SceneError if any component is not finite, if `intensity` is negative, or if
		 *         `direction` has zero length.
		 */
		virtual void
		SetDirectionalLight(const DirectionalLightDesc& desc) = 0;

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

		/**
		 * Sets the wind every grass field in this view sways in. Replaces any previous wind; a view
		 * that never calls this is calm. Per view, like the light, so two views of one Scene may
		 * blow differently.
		 *
		 * Not a temporal-epoch change: grass evaluates the wind at this frame's time and the last
		 * one's, so the change arrives as motion. A new wind takes effect in both evaluations at
		 * once, which reads as a one-frame jump in the velocity rather than a smear.
		 *
		 * @throws SceneError if any field is not finite; if `strength`, `gustStrength` or
		 *         `gustSpeed` is negative; if `gustScale` is not positive; or if `direction` has
		 *         no horizontal length.
		 */
		virtual void
		SetWind(const WindDesc& desc) = 0;

	protected:
		ISceneView() noexcept = default;
	};

	using SceneViewRef = core::SharedRef<ISceneView>;
}

template class BGL_API core::SharedRef<bgl::ISceneView>;
