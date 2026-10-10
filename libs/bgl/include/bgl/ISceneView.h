#pragma once
#include <bgl/IMeshInstanceWriter.h>
#include <bgl/IScene.h>
#include <bgl/api.h>
#include <bgl/types/BackdropGradient.h>
#include <bgl/types/BlobShadowDesc.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <bgl/types/EnvironmentMapDesc.h>
#include <bgl/types/FootIKDesc.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/InstanceDesc.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/MeshInstanceBlockDesc.h>
#include <bgl/types/MeshInstanceBlockHandle.h>
#include <bgl/types/MeshInstanceFlags.h>
#include <bgl/types/MeshInstanceHandle.h>
#include <bgl/types/SkinnedMeshInstanceDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/ToonShadingRigHandle.h>
#include <bgl/types/WindDesc.h>
#include <bgpu/uniforms/UniformsBase.h>
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
		CreateStaticMeshInstance(const StaticMeshInstanceDesc& desc) = 0;

		/**
		 * The kSkinnedMesh counterpart of CreateStaticMeshInstance: the placement `desc` describes,
		 * its record posed from its source -- see SkinnedMeshInstanceDesc. Deleted through the same
		 * DeleteMeshInstance as any other placement.
		 *
		 * @throws SceneError if `desc.geom` is not a live kSkinnedMesh geom; a slot names a node
		 *         past the rig's table, a weight is negative, a ramp ends before it starts, a value
		 *         is not finite, or no slot carries any weight; `desc.source` is kAuto, which no pass
		 *         draws yet; with kBoneAnimTable, the record is not one clip, or the rig's table
		 *         cannot be reserved.
		 * @post With kBoneAnimTable, the *first* such instance on a rig reserves its table:
		 *       `boneCount * frameCount` skinning matrices of device memory, tens of megabytes on a
		 *       dense rig, filled by the next frame this view is drawn. Later instances on the same
		 *       rig cost nothing.
		 */
		virtual MeshInstanceHandle
		CreateSkinnedMeshInstance(const SkinnedMeshInstanceDesc& desc) = 0;

		/**
		 * Rewrites a skinned placement's playback record in place, on the per-instance or the
		 * automatic source. The instance keeps
		 * its pose storage and its place among the instances posed each frame; only what is
		 * evaluated changes, from the next frame drawn.
		 *
		 * The caller owns the property that makes this safe under temporal reprojection: the new
		 * record evaluated at the previous frame's time must give the pose the old one did. See
		 * SkinnedPlaybackDesc.
		 *
		 * @throws SceneError if the handle is invalid or removed, the placement is not a skinned
		 *         one on the per-instance or automatic source, or `desc` fails the checks
		 *         CreateSkinnedMeshInstance makes.
		 */
		virtual void
		SetSkinnedPlayback(MeshInstanceHandle instance, const SkinnedPlaybackDesc& desc) = 0;

		/**
		 * The record SetSkinnedPlayback or the spawn wrote, so a caller need not keep a copy.
		 *
		 * @throws SceneError if the handle is invalid or removed, or the placement is not a skinned
		 *         one on the per-instance or automatic source.
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
		 * Rewrites the runtime foot-IK weights of a skinned instance on the per-instance or the
		 * automatic source -- see FootIKDesc. On the automatic source they apply while it draws per
		 * instance, scaled toward zero as it dissolves to its table. Written on an event and evaluated from RenderJob::time, so the pose at
		 * any clock is a function of the record: a write whose ramps all start at or after now
		 * leaves the pose the previous frame drew unchanged, which is what keeps that frame's
		 * motion vector exact. FootIKDesc::FadeTo builds such a write from GetFootIK's record.
		 *
		 * @throws SceneError if the handle is invalid or removed, the placement is not a skinned
		 *         one on the per-instance or automatic source, its rig authored no legs, or a stored
		 *         leg's ramp
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
		 * Whether `instance` owns a foot-IK record: a live skinned placement on the per-instance or
		 * automatic source whose rig authored legs. Exactly when SetFootIK and GetFootIK would not
		 * throw, for a caller that cannot tell a rig's legs from the outside.
		 */
		[[nodiscard]] virtual bool
		HasFootIK(MeshInstanceHandle instance) const noexcept = 0;

		/**
		 * Whether `instance` is a live skinned placement whose rig authored legs, on any source:
		 * one whose feet a pose can find, planted or not.
		 */
		[[nodiscard]] virtual bool
		HasLegs(MeshInstanceHandle instance) const noexcept = 0;

		/**
		 * Gives one placement a blob shadow: a soft radial-falloff disc draped over the static
		 * geometry directly beneath it, fading per pixel as the gap between the placement and the
		 * surface grows -- see BlobShadowDesc. Only static, upward-facing surfaces receive it, and
		 * grass, whose blades take it as the ground they grow from: a unit never catches a
		 * neighbour's shadow, and a wall beside the placement keeps its face.
		 * Replaces any blob shadow the placement holds. Any placement may carry one; the expected
		 * consumers are skinned units, which nothing enforces. `desc.feet` adds a shadow under each
		 * foot, read from the pose the frame draws -- see FootShadowDesc.
		 *
		 * @throws SceneError if the handle is invalid or removed, `desc.radius` or
		 *         `desc.fadeHeight` is not finite and positive, `desc.intensity` is not
		 *         finite in [0, 1], or `desc.casterLift` is not finite and non-negative; or if
		 *         `desc.feet` is set on a placement HasLegs refuses, or holds a field outside
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
		 * Gives one placement a face's toon shading rig: each frame its edits are evaluated against the
		 * sun in the placement's head space and drawn on the pixels of its toon character surfaces
		 * that are face (`ToonCharacterSurface::face`). Faded out by the head's projected size --
		 * see ToonShadingRigDesc. Replaces any rig the placement holds, releasing its use.
		 *
		 * The placement holds a use of the rig until ClearToonShadingRig, a replacing call,
		 * DeleteMeshInstance or this view's destruction; see IScene::DeleteToonShadingRig.
		 *
		 * @throws SceneError if the instance handle is invalid or removed; `rig` is null or deleted;
		 *         or the rig names a head bone and the placement is not a skinned one whose geom is
		 *         alive, or the bone is not in its rig.
		 */
		virtual void
		SetToonShadingRig(MeshInstanceHandle instance, ToonShadingRigHandle rig) = 0;

		/**
		 * Removes the placement's toon shading rig and releases its use. A no-op on a placement that
		 * holds none.
		 *
		 * @throws SceneError if the handle is invalid or already removed.
		 */
		virtual void
		ClearToonShadingRig(MeshInstanceHandle instance) = 0;

		/**
		 * The rig SetToonShadingRig last gave the placement, or a null handle.
		 *
		 * @throws SceneError if the handle is invalid or already removed.
		 */
		[[nodiscard]] virtual ToonShadingRigHandle
		GetToonShadingRig(MeshInstanceHandle instance) const = 0;

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
		 * Sets the view's one sun, which every shading model reads: PBR, the lit surfaces
		 * (ILitSurfaceSource), grass, and the toon character model and its shading rigs. A sun
		 * casting no shadow; replaces any previously set one. Per-view for the same reason the
		 * environment is -- two views of one Scene are lit independently.
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
		 * Draws `backdrop` behind the scene in place of the skybox until ClearBackdrop. Only what is
		 * drawn behind changes: the environment lights the scene as before, and a SetSkyBox while it
		 * is up is the sky ClearBackdrop shows. A view with no skybox draws it too.
		 *
		 * @throws SceneError if a colour component is not finite or is negative.
		 */
		virtual void
		SetBackdrop(const BackdropGradient& backdrop) = 0;

		/** Draws the skybox behind the scene again, if the view has one; a no-op with no backdrop. */
		virtual void
		ClearBackdrop() noexcept = 0;

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

		/**
		 * Sets how this view chooses each placement's level of detail -- see LodSelectionDesc.
		 * Replaces the previous choice; a view that never calls this draws every mesh as authored.
		 * Per view, like the light: two views of one Scene may hold detail differently. A setting
		 * rather than a per-frame or per-placement call -- set it when the view is made or its
		 * quality changes; each placement's level is the cull's to choose.
		 *
		 * Takes effect on the next frame this view is drawn, through the same dissolve a change of
		 * size causes, so it is not an epoch change.
		 *
		 * @throws SceneError if `pixelScale` is not finite and positive, `fadeSeconds` is not finite
		 *         and non-negative, or `forceLevel` is not a level
		 *         (LodLevel::kCount or past it).
		 */
		virtual void
		SetLodSelection(const LodSelectionDesc& desc) = 0;

		/** The record SetLodSelection last wrote, or the default. */
		[[nodiscard]] virtual LodSelectionDesc
		GetLodSelection() const noexcept = 0;

		/**
		 * Whether this view culls its occludees -- the static tier's opaque placements -- against
		 * the depth of its own previous draw before drawing them, so what stands behind terrain or
		 * behind another placement is never drawn. On by default. A setting rather than a per-frame
		 * call, like SetLodSelection, and like it per view and not an epoch change: the frame it
		 * draws is the same either way, only its cost differs, which is what a benchmark turns it
		 * off to measure.
		 */
		virtual void
		SetOcclusionCulling(bool enabled) noexcept = 0;

		[[nodiscard]] virtual bool
		GetOcclusionCulling() const noexcept = 0;

		/**
		 * Reserves `desc.capacity` placements of one geom that a GPU kernel places every
		 * frame -- see SetBlockWriter -- and the CPU never writes again: a crowd, or mesh
		 * particles. Its placements have no handles, so nothing here moves, flags or deletes one
		 * alone. Every placement starts hidden, and a block with no writer stays hidden. The geom's
		 * grass is not grown on them: grass is laid out on the CPU, which never sees where a writer
		 * put a placement.
		 *
		 * A skinned geom's placements are on PoseSource::kAuto and share one playback record,
		 * `desc.playback`: each is posed per instance or drawn from its rig's table as the view's
		 * LodSelectionDesc chooses, a hidden one taking no pose, and plays the record ahead of the
		 * clock by the offset its writer gives it, through ISkinnedMeshInstanceBlock: its writer is
		 * one compiled for skinned blocks. They are reached by no playback, foot-IK,
		 * blob-shadow or selection call, which all take a placement's handle.
		 *
		 * Moves the temporal epoch once, as one placement's creation does. Nothing the writer does
		 * moves it: a placement it shows or hides writes its own motion.
		 *
		 * Like a placement, the block names its geom and does not own it: deleting the geom first
		 * leaves the block reading whatever record takes its slot.
		 *
		 * @throws SceneError if `desc.geom` is not a live static or skinned geom, `desc.capacity` is 0
		 *         or past c_MaxMeshInstanceBlockCapacity, or a skinned geom's `desc.playback` is one
		 *         CreateSkinnedMeshInstance refuses, or `desc.toonShadingRig` is one SetToonShadingRig
		 *         would refuse for a placement of the geom.
		 */
		virtual MeshInstanceBlockHandle
		CreateMeshInstanceBlock(const MeshInstanceBlockDesc& desc) = 0;

		/**
		 * Releases a block's placements, moving the temporal epoch once.
		 *
		 * @throws SceneError if the handle is invalid or already removed.
		 */
		virtual void
		DeleteMeshInstanceBlock(MeshInstanceBlockHandle block) = 0;

		/**
		 * Binds the kernel that places `block` from the next frame this view is drawn, or unbinds it
		 * with null, which hides every placement again. Rebinding replaces the block's parameters
		 * with a fresh set, every value zero and every handle null.
		 *
		 * @throws SceneError if the handle is invalid or removed, `writer` was created by another
		 *         IGraphics, or it writes the other kind of geom's blocks
		 *         (MeshInstanceWriterDesc::geomType).
		 */
		virtual void
		SetBlockWriter(MeshInstanceBlockHandle block, MeshInstanceWriterRef writer) = 0;

		/**
		 * The block's copy of its writer's `Params`: what the writer reads for this block, written
		 * by name as any constant buffer is -- `params["agents"] = buffer->GetHandle()`. Kept across
		 * frames; a frame draws whatever it holds when the view is drawn. Valid until the block is
		 * deleted or rebound.
		 *
		 * @throws SceneError if the handle is invalid or removed, or the block has no writer.
		 */
		[[nodiscard]] virtual bgpu::UniformsBase::Accessor
		GetBlockParams(MeshInstanceBlockHandle block) = 0;

	protected:
		ISceneView() noexcept = default;
	};

	using SceneViewRef = core::SharedRef<ISceneView>;
}

template class BGL_API core::SharedRef<bgl::ISceneView>;
