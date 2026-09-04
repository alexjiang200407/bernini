#pragma once
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/GeomHandle.h>
#include <bgl/GeomType.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/RigHandle.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/api.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl/types/ChannelRouteDesc.h>
#include <bgl/types/EnvironmentMapDesc.h>
#include <bgl/types/FootPlantDesc.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl/types/LoosePbrMaterialDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <core/containers/slot_handle.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class SceneError : public ApiError
	{
	public:
		SceneError() = delete;
		using ApiError::ApiError;
	};

	class BGL_API IScene : public core::Ref
	{
	public:
		IScene(IScene&&) noexcept      = delete;
		IScene(const IScene&) noexcept = delete;

		IScene&
		operator=(IScene&&) noexcept = delete;

		IScene&
		operator=(const IScene&) noexcept = delete;

		virtual const SceneDesc&
		GetDesc() const noexcept = 0;

		virtual GeomHandle
		AddCubeGeom(MaterialHandle material = {}) = 0;

		/**
		 * Adds a procedurally generated UV sphere of `radius`, centred on the origin, as static-mesh
		 * geometry.
		 *
		 * @throws SceneError if either segment count is 0, or a buffer allocation fails.
		 */
		virtual GeomHandle
		AddSphereGeom(
			uint32_t       xSegments,
			uint32_t       ySegments,
			float          radius,
			MaterialHandle material = {}) = 0;

		/**
		 * Adds a procedurally generated plane as static-mesh geometry: a flat `width` x `height` quad
		 * centred on the origin, subdivided into an `xSegments` x `ySegments` grid.
		 * @throws SceneError if either segment count is 0, if the grid is larger than one draw can
		 *         launch, or if a buffer allocation fails.
		 */
		virtual GeomHandle
		AddPlaneGeom(
			uint32_t       xSegments,
			uint32_t       ySegments,
			float          width,
			float          height,
			MaterialHandle material = {}) = 0;

		/**
		 * Adds one mesh of a loaded BMesh as static-mesh geometry, uploading its submeshes'
		 * geometry into this scene's buffers. Each submesh is bound to
		 * `materials[submesh.material]`; a submesh whose material index is out of range (e.g. the
		 * source had none) is left unlit.
		 *
		 * @param mesh       A BMesh loaded from disk (see assetlib::load).
		 * @param meshIndex  Index into `mesh.meshes`.
		 * @param materials  Materials parallel to `mesh.materials`, resolved by the caller.
		 * @throws SceneError if `meshIndex` is out of range or a buffer allocation fails.
		 */
		virtual GeomHandle
		AddStaticMeshGeom(
			const assetlib::BMesh&          mesh,
			uint32_t                        meshIndex,
			std::span<const MaterialHandle> materials) = 0;

		/**
		 * The commit half of the AddStaticMeshGeom split: uploads a mesh CookStaticMesh flattened,
		 * consuming it. Cook on a worker, commit here -- the flattening is the dominant cost of a
		 * large mesh, and the overload above pays it on the calling thread.
		 *
		 * @param mesh       From CookStaticMesh. Consumed, even on failure.
		 * @param materials  Materials parallel to the source BMesh's `materials`, resolved by the
		 *                   caller; a submesh whose material index is out of range is left unlit.
		 * @throws SceneError if `mesh` was already consumed, or a buffer allocation fails.
		 */
		virtual GeomHandle
		AddStaticMeshGeom(PreparedStaticMesh mesh, std::span<const MaterialHandle> materials) = 0;

		/**
		 * Uploads a rig -- a skeleton and the clips cooked against it -- as a scene object of its
		 * own: the bone table, the clip table and the frame-major sample pool, shared by every geom
		 * skinned to it. A modular unit is several meshes on one rig, and each of those meshes
		 * addresses the same bones by the same indices, so the rig is uploaded once and named by
		 * handle rather than re-uploaded per mesh.
		 *
		 * Takes the containers as they are: `Skeleton` and `AnimationSet` are `assetlib_structs`
		 * PODs with nothing to decode, so neither is mirrored into a desc. `footPlant` is the
		 * exception, and carries only what no container holds: bone *names* resolved to indices and
		 * a sole plane measured off the mesh, neither of which bgl can derive without assetlib. It
		 * belongs to the rig and not to a geom on it -- a leg is bone indices into `skeleton` and a
		 * weight per frame of `animations`, so two meshes on one rig plant the same feet.
		 *
		 * @param skeleton   The bones the clips and every skinned mesh address by bare index.
		 * @param animations Clips cooked against `skeleton`.
		 * @param footPlant  The rig's legs and their per-frame plant weights, or empty for a rig
		 *                   that plants no feet -- which animates exactly as it did before.
		 * @throws SceneError for a skeleton with no bones, bones that are not topologically sorted,
		 *         an `animations` whose bone count disagrees with `skeleton`, an empty or
		 *         zero-frame clip table, a clip whose frames fall outside the sample pool, more legs
		 *         than `idl::cMaxLegsPerRig`, a leg naming a bone outside the skeleton or a chain
		 *         whose links are not directly parented, a leg whose sole normal is not finite and
		 *         nonzero, or a `plantWeights` that is not one byte per leg for every frame in the
		 *         sample pool.
		 *
		 * `AnimationSet::skeletonSignature` is deliberately **not** checked here: computing a
		 * skeleton's signature needs assetlib, which bgl does not link. A clip set cooked against a
		 * since-reordered rig of the same bone count therefore passes this door and animates
		 * wrongly. Whoever loaded the two containers owns that check -- gamelib's acquire makes it.
		 */
		virtual RigHandle
		AddRig(
			const assetlib::Skeleton&     skeleton,
			const assetlib::AnimationSet& animations,
			const FootPlantDesc&          footPlant = {}) = 0;

		/**
		 * Destroys a rig, releasing its bone, clip and sample ranges.
		 *
		 * @pre No geom added against this rig is still alive. Unlike a texture asset, whose
		 *      dangling use only misrenders, a geom outliving its rig would pose from freed ranges
		 *      -- so this is refused rather than permitted, and the caller deletes its geoms first.
		 * @param rig A handle returned by AddRig.
		 * @throws SceneError if the handle is null, already deleted, or still has a live geom.
		 */
		virtual void
		DeleteRig(RigHandle rig) = 0;

		/**
		 * Adds one mesh of a loaded BMesh as skinned geometry against `rig`: the bind-pose submeshes
		 * upload exactly as AddStaticMeshGeom does, and every instance's pose is computed each frame
		 * from the rig instead of being fetched from a bake.
		 *
		 * The rig is shared, not consumed: several meshes may be added against one, which is what a
		 * unit assembled from slot meshes draws as. The rig must outlive every geom added to it.
		 *
		 * Each submesh must carry `joints0` and `weights0` -- a submesh with no skin binding has no
		 * bones to follow and would draw its bind pose while the rest of the mesh moved.
		 *
		 * Every submesh's culling sphere comes from `posedBounds`, not its bind pose: the bind pose's
		 * box stops holding once a limb moves or a clip's root motion carries the rig out of it. bgl
		 * cannot measure the box itself: reading a vertex's influences means decoding a vertex
		 * layout, which lives in assetlib. Whoever loaded the containers supplies it -- read off the
		 * `.banim`'s bake (`assetlib::findPosedBounds`)
		 * or measured (`assetlib::posedBounds`), which is gamelib's acquire either way.
		 *
		 * `materials` must resolve every submesh to a `kPBR` material, in any layer: the skinned
		 * pipeline shades through the PBR pixel stages and has no unlit or loose variant.
		 *
		 * @param mesh        A BMesh loaded from disk, carrying skin binding on every submesh.
		 * @param meshIndex   Index into `mesh.meshes`.
		 * @param materials   Materials parallel to `mesh.materials`, resolved by the caller.
		 * @param rig         The rig the mesh's joint indices address, from AddRig.
		 * @param posedBounds A box holding the mesh in every pose of every clip, in model space.
		 * @throws SceneError for anything AddStaticMeshGeom refuses, a null or deleted `rig`, a
		 *         submesh without skin binding, a submesh whose material does not resolve to kPBR,
		 *         or a `posedBounds` whose min exceeds its max on any axis.
		 */
		virtual GeomHandle
		AddSkinnedMeshGeom(
			const assetlib::BMesh&          mesh,
			uint32_t                        meshIndex,
			std::span<const MaterialHandle> materials,
			RigHandle                       rig,
			const assetlib::Bounds&         posedBounds) = 0;

		virtual TextureAssetHandle
		AddTextureAsset(assetlib::ImageData img, std::string debugName = "") = 0;

		/**
		 * Destroys a texture asset, releasing its GPU resource and the slot a shader reached it by.
		 * The release is deferred until the frames that could still be sampling it have completed.
		 *
		 * The scene does not know which materials sample a texture. Deleting one that a live
		 * material still routes leaves that material reading a slot that a later AddTextureAsset
		 * may reuse; delete such materials first.
		 *
		 * A view's SetEnvironmentMap / SetSkyBox bindings are the same hazard and are not cascaded
		 * to: rebind the view before releasing what it named. The retired slot is not benign on
		 * every renderer -- one may resolve the handle as it draws, and abort on the next frame.
		 *
		 * @param texture A handle returned by AddTextureAsset.
		 * @throws SceneError if the handle is null, or already deleted.
		 */
		virtual void
		DeleteTextureAsset(TextureAssetHandle texture) = 0;

		/**
		 * Creates a PBR material in this scene's material arena and returns a handle
		 * referencing it. Pass the handle to a geometry-creating method to bind it.
		 */
		virtual MaterialHandle
		CreatePbrMaterial(const PbrMaterialDesc& desc) = 0;

		/**
		 * Creates a loose (unbaked, per-channel) PBR material in the same arena as CreatePbrMaterial
		 * and returns a handle referencing it. Bind it like any material; it renders through the same
		 * lighting path as a PbrMaterial. See LoosePbrMaterialDesc.
		 */
		virtual MaterialHandle
		CreateLoosePbrMaterial(const LoosePbrMaterialDesc& desc) = 0;

		/**
		 * Rewrites a material's contents in place, keeping its handle and its bytes in the arena.
		 *
		 * A submesh stores the material's *byte offset*, so every submesh bound to `material` picks
		 * the new textures and factors up with no rebinding -- this is how a texture is swapped on a
		 * live material. How a submesh is drawn derives from the material's *type*, which an update
		 * cannot change, so that is unaffected too.
		 *
		 * @throws SceneError if the handle is invalid, expired, or not of the matching type.
		 */
		virtual void
		UpdatePbrMaterial(MaterialHandle material, const PbrMaterialDesc& desc) = 0;

		/** The loose (per-channel) counterpart of UpdatePbrMaterial. */
		virtual void
		UpdateLoosePbrMaterial(MaterialHandle material, const LoosePbrMaterialDesc& desc) = 0;

		/**
		 * Destroys a material created by CreatePbrMaterial or CreateLoosePbrMaterial, freeing its
		 * record in the scene's material arena.
		 *
		 * A submesh stores the material's byte offset, not a generation-checked handle, so a submesh
		 * still bound to a deleted material silently picks up whichever record next takes those
		 * bytes -- of any kind, and possibly the middle of one, the arena holding records of
		 * different sizes. Rebind every submesh using it (SetSubmeshMaterial) before deleting.
		 *
		 * @param material A handle returned by a material-creating method.
		 * @throws SceneError if the handle is invalid, already deleted, disagrees with the type of
		 *         the record it names, or names a material type that has no storage to free (kNull,
		 *         kAssert).
		 */
		virtual void
		DeleteMaterial(MaterialHandle material) = 0;

		/**
		 * Sets the **default** material of one submesh of a geom. `submeshIndex` is relative to the
		 * geom's submesh range.
		 *
		 * @throws SceneError if the geom handle is invalid, the material is invalid, or the submesh
		 *         index is out of range.
		 */
		virtual void
		SetSubmeshMaterial(GeomHandle geom, uint32_t submeshIndex, MaterialHandle material) = 0;

		/**
		 * Sets the ground plane every skinned instance in this scene plants its feet on. Scene-wide
		 * rather than per view: every view of a scene stands on the same ground, where lighting is
		 * per view because two views may be exposed differently.
		 *
		 * A change no motion vector describes, so it moves the temporal epoch: the frame after it
		 * is taken whole rather than reprojected through a pose solved against ground that was not
		 * there. Set it with the scene, not every frame.
		 *
		 * @param ground `normal` need not be unit length; it is normalised on the way in.
		 * @throws SceneError if `point` or `normal` is not finite, `normal` is zero, or
		 *         `normal.y <= 0` -- a vertical or overhanging plane has no height under a point.
		 */
		virtual void
		SetGround(const GroundPlaneDesc& ground) = 0;

		/** The plane SetGround last accepted, with its normal unit length; the default until then. */
		[[nodiscard]] virtual const GroundPlaneDesc&
		GetGround() const noexcept = 0;

		/**
		 * Whether skinned instances in this scene plant their feet at all. On by default; off, a rig
		 * that authored legs poses exactly as one that did not -- the same clip against the same
		 * ground with and without the solve, which is what judging the solve takes.
		 *
		 * Scene-wide like the ground, and like it a change no motion vector describes, so it moves
		 * the temporal epoch.
		 */
		virtual void
		SetFootPlanting(bool enabled) noexcept = 0;

		[[nodiscard]] virtual bool
		GetFootPlanting() const noexcept = 0;

		/**
		 * Whether `geom` still refers to live geometry in this scene.
		 */
		[[nodiscard]] virtual bool
		IsGeomAlive(GeomHandle geom) const noexcept = 0;

		/**
		 * Removes geometry and frees the buffers behind it.
		 *
		 * @pre Every mesh instance placed from this geom has been destroyed
		 *      (ISceneView::DeleteMeshInstance). The scene does not track instances and cannot
		 *      check: an instance holds a plain copy of the geom's submesh range, with no
		 *      generation, so one that outlives its geometry will draw whatever geometry is
		 *      allocated into that range next. Owning that lifetime is the caller's job.
		 *
		 * @param geom A handle returned by a geometry-creating method.
		 * @throws SceneError if the handle is invalid or already removed.
		 */
		virtual void
		DeleteGeom(GeomHandle geom) = 0;

	protected:
		IScene() noexcept = default;
	};

	using SceneRef = core::SharedRef<IScene>;
}

template class BGL_API core::SharedRef<bgl::IScene>;
