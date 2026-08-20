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
#include <bgl/PsoType.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/api.h>
#include <bgl/error.h>
#include <bgl/glm.h>
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

	/**
	 * How big the scene's GPU arenas start, not how big they may get. Each one grows on demand,
	 * bounded only by device memory, so these are a hint that trades startup residency against the
	 * number of growth events during a load. Sizing them near the steady state avoids both.
	 */
	struct SceneDesc
	{
		uint32_t initialGeom                 = 1;
		uint32_t initialMeshlets             = 1;
		uint32_t initialIndices              = 1;
		uint32_t initialSubmeshes            = 1;
		uint32_t initialVertexBufferByteSize = 1;
		uint32_t initialPbrMaterials         = 1;
		uint32_t initialLoosePbrMaterials    = 1;
	};

	/**
	 * The decoded IBL cube maps: the diffuse and specular convolutions of one environment.
	 *
	 * The split-sum BRDF table that completes the specular term is not here. It integrates a white
	 * environment, so it belongs to the shading model rather than to any environment, and bgl
	 * generates its own at device init -- there is nothing for a caller to supply or to mismatch.
	 */
	struct EnvironmentMapDesc
	{
		EnvironmentMapDesc() = default;

		EnvironmentMapDesc(TextureAssetHandle irr, TextureAssetHandle pre) :
			irradiance(irr), prefilter(pre)
		{}

		EnvironmentMapDesc(EnvironmentMapDesc&&) noexcept = default;
		EnvironmentMapDesc(const EnvironmentMapDesc&)     = delete;

		EnvironmentMapDesc&
		operator=(EnvironmentMapDesc&&) noexcept = default;

		EnvironmentMapDesc&
		operator=(const EnvironmentMapDesc&) = delete;

		TextureAssetHandle irradiance;
		TextureAssetHandle prefilter;
	};

	struct PbrMaterialDesc
	{
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		float     metallicFactor  = 1.0f;
		float     roughnessFactor = 1.0f;

		LayerType layerType   = LayerType::kOpaque;
		float     alphaCutoff = 0.5f;

		// What baseColorFactor.a means on a kBlend surface, and read by no other layer: 0 coverage
		// (hair, foliage), 1 transmission (glass). glTF's KHR_materials_transmission.
		float transmissionFactor = 0.0f;

		// glTF's KHR_materials_specular. The colour tints a dielectric's F0 away from grey; the
		// factor weights the whole specular lobe, so 0 is a surface with no reflection at all.
		glm::vec3 specularColorFactor = glm::vec3(1.0f);
		float     specularFactor      = 1.0f;

		// Optional material maps, from AddTextureAsset.
		TextureAssetHandle baseColorTexture;
		TextureAssetHandle normalTexture;
		TextureAssetHandle ormTexture;
	};

	struct ChannelRouteDesc
	{
		TextureAssetHandle texture;
		uint16_t           channel = 0;  // 0 = R, 1 = G, 2 = B, 3 = A
	};

	/** One vertex of a VAT geom's bind-pose mesh -- the full procedural layout, tightly packed. */
	struct VatVertex
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec4 tangent;
	};

	/** One clip's rows of the VAT texture pair; see VatGeomDesc. */
	struct VatClipDesc
	{
		uint32_t firstRow   = 0;  // texture V of frame 0
		uint32_t frameCount = 0;  // real frames; the bake pads a duplicate row after the last
		float    sampleRate = 30.0f;
		bool     loop       = false;
	};

	/**
	 * A rig's baked clip set, as textures already uploaded through AddTextureAsset: positions
	 * `R16G16B16A16_UNORM` unorm-packed in [boundsMin, boundsMax] -- one box over every frame of
	 * every clip -- and normals `R8G8B8A8_UNORM`, `rgb` the unit object-space normal as
	 * `xyz * 0.5 + 0.5` and `a` the tangent's twist about it, `radians / 2pi + 0.5` (see
	 * docs/vat.md). Columns are geometry-local vertex indices; frame `f` of clip `c` is row
	 * `clips[c].firstRow + f`, which is the row index the shared idl::Clip carries as `firstFrame`.
	 *
	 * bgl never reads a `.bvat` (it stays codec-free); whoever decodes one -- gamelib, or a test
	 * synthesizing textures from scratch -- fills this in.
	 */
	struct VatGeomDesc
	{
		TextureAssetHandle positions;
		TextureAssetHandle normals;

		glm::vec3 boundsMin = glm::vec3(0.0f);
		glm::vec3 boundsMax = glm::vec3(1.0f);

		std::vector<VatClipDesc> clips;

		// Where each submesh's vertex columns start along U, in submesh order -- the bake's
		// VatColumns::columnBase values. Empty means a single submesh at column 0, which is what
		// AddVatMeshGeom uploads; AddVatMeshGeom requires one entry per submesh.
		std::vector<uint32_t> columnBases;
	};

	struct LoosePbrMaterialDesc
	{
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		float     metallicFactor  = 1.0f;
		float     roughnessFactor = 1.0f;

		// Cutout; see PbrMaterialDesc. A loose material routes its alpha explicitly (baseColor[3]),
		// so unlike a baked one it can always sample a real alpha channel.
		LayerType layerType   = LayerType::kOpaque;
		float     alphaCutoff = 0.5f;

		// Coverage against transmission; see PbrMaterialDesc.
		float transmissionFactor = 0.0f;

		// Dielectric F0 tint and specular strength; see PbrMaterialDesc.
		glm::vec3 specularColorFactor = glm::vec3(1.0f);
		float     specularFactor      = 1.0f;

		std::array<ChannelRouteDesc, 4> baseColor;  // R, G, B, A
		std::array<ChannelRouteDesc, 3> orm;        // AO, roughness, metallic
		std::array<ChannelRouteDesc, 2> normal;     // X, Y (Z reconstructed in shader)
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
		 * @throws SceneError if either segment count is 0, if the grid needs more meshlets than one
		 *         dispatch can launch, or if a buffer allocation fails.
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
		 * vertex / index / meshlet data into this scene's buffers. Each submesh is bound to
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
		 * Adds VAT geometry: the bind-pose mesh whose meshlets and UVs every instance draws, bound
		 * to the baked texture pair its vertices are fetched from (see VatGeomDesc). The submesh's
		 * culling bounds come from the desc's box, not the bind pose -- they must hold under every
		 * frame of every clip.
		 *
		 * `material` is required, opaque `kPBR` only: the VAT pipeline shades through the PBR pixel
		 * stage, and no other variant of it exists yet. The same constraint holds for
		 * SetSubmeshMaterial on this geom.
		 *
		 * @throws SceneError if a texture handle or the material is invalid or of the wrong kind,
		 *         `clips` is empty, the primitive needs more meshlets than one dispatch can launch,
		 *         or a buffer allocation fails.
		 */
		virtual GeomHandle
		AddVatMeshGeom(
			std::span<const VatVertex> verts,
			std::span<const uint32_t>  indices,
			const VatGeomDesc&         desc,
			MaterialHandle             material) = 0;

		/**
		 * Adds one mesh of a loaded BMesh as VAT geometry: the bind-pose submeshes upload exactly
		 * as AddStaticMeshGeom does -- cooked meshlets, one GPU submesh per source submesh, materials
		 * resolved by each submesh's material index -- and every instance fetches position and
		 * normal from the desc's texture pair instead of the vertex bytes. Every submesh's culling
		 * sphere comes from the desc's all-clips box, not its bind pose: the bind pose's bounds do
		 * not hold once a limb moves.
		 *
		 * `desc.columnBases` must carry one entry per submesh of `meshes[meshIndex]`, in submesh
		 * order -- the bake's per-submesh column bases. Every submesh must resolve to a valid
		 * opaque `kPBR` material: the VAT pipeline has no null or cutout variant for an unlit or
		 * masked submesh to ride.
		 *
		 * @throws SceneError for anything AddStaticMeshGeom or AddVatMeshGeom refuses, a columnBases count
		 *         that does not match the submesh count, or a submesh whose material does not
		 *         resolve to opaque kPBR.
		 */
		virtual GeomHandle
		AddVatMeshGeom(
			const assetlib::BMesh&          mesh,
			uint32_t                        meshIndex,
			std::span<const MaterialHandle> materials,
			const VatGeomDesc&              desc) = 0;

		/**
		 * Adds one mesh of a loaded BMesh as skinned geometry: the bind-pose submeshes upload exactly
		 * as AddStaticMeshGeom does, and every instance's pose is computed each frame from `skeleton`
		 * and `animations` instead of being fetched from a bake. The rig's bones, clip table and
		 * sample pool upload with the geometry and are shared by every instance of it.
		 *
		 * Unlike VAT this takes the containers as they are: `Skeleton` and `AnimationSet` are
		 * `assetlib_structs` PODs with nothing to decode, so there is no desc to mirror them into.
		 *
		 * Each submesh must carry `joints0` and `weights0` -- a submesh with no skin binding has no
		 * bones to follow and would draw its bind pose while the rest of the mesh moved.
		 *
		 * Every submesh's culling sphere comes from `posedBounds`, not its bind pose -- the same rule
		 * VAT follows, and for the same reason: the bind pose's box stops holding once a limb moves or
		 * a clip's root motion carries the rig out of it. bgl cannot measure the box itself:
		 * skinning a vertex means decoding a vertex layout, which lives in assetlib. Whoever loaded
		 * the containers measures it -- `assetlib::posedBounds` is that walk, and gamelib's acquire
		 * makes it.
		 *
		 * `materials` must resolve every submesh to an opaque `kPBR` material, the same constraint
		 * VAT carries and for the same reason: no other variant of the pipeline exists.
		 *
		 * @param mesh        A BMesh loaded from disk, carrying skin binding on every submesh.
		 * @param meshIndex   Index into `mesh.meshes`.
		 * @param materials   Materials parallel to `mesh.materials`, resolved by the caller.
		 * @param skeleton    The rig the mesh's joint indices address.
		 * @param animations  Clips cooked against `skeleton`.
		 * @param posedBounds A box holding the mesh in every pose of every clip, in model space.
		 * @throws SceneError for anything AddStaticMeshGeom refuses, a skeleton with no bones or more
		 *         than `cMaxBonesPerRig`, bones that are not topologically sorted, an `animations`
		 *         whose bone count disagrees with `skeleton`, an empty or zero-frame clip table, a
		 *         clip whose samples fall outside the pool, a submesh without skin binding, a submesh
		 *         whose material does not resolve to opaque kPBR, or a `posedBounds` whose min exceeds
		 *         its max on any axis.
		 *
		 * `AnimationSet::skeletonSignature` is deliberately **not** checked here: computing a
		 * skeleton's signature needs assetlib, which bgl does not link. A clip set cooked against a
		 * since-reordered rig of the same bone count therefore passes this door and animates wrongly.
		 * Whoever loaded the two containers owns that check -- gamelib's acquire makes it.
		 */
		virtual GeomHandle
		AddSkinnedMeshGeom(
			const assetlib::BMesh&          mesh,
			uint32_t                        meshIndex,
			std::span<const MaterialHandle> materials,
			const assetlib::Skeleton&       skeleton,
			const assetlib::AnimationSet&   animations,
			const assetlib::Bounds&         posedBounds) = 0;

		virtual TextureAssetHandle
		AddTextureAsset(assetlib::ImageData img, std::string debugName = "") = 0;

		/**
		 * Destroys a texture asset, releasing its GPU resource and its bindless descriptor slot.
		 * The release is deferred until the frames that could still be sampling it have completed.
		 *
		 * The scene does not know which materials sample a texture. Deleting one that a live
		 * material still routes leaves that material reading a slot that a later AddTextureAsset
		 * may reuse; delete such materials first.
		 *
		 * A view's SetEnvironmentMap / SetSkyBox bindings are the same hazard and are not cascaded
		 * to: rebind the view before releasing what it named. The retired slot is not benign on
		 * every backend -- Metal resolves the handle at dispatch and aborts on the next frame.
		 *
		 * @param texture A handle returned by AddTextureAsset.
		 * @throws SceneError if the handle is null, or already deleted.
		 */
		virtual void
		DeleteTextureAsset(TextureAssetHandle texture) = 0;

		/**
		 * Creates a PBR material in this scene's material buffer and returns a handle
		 * referencing it. Pass the handle to a geometry-creating method to bind it.
		 */
		virtual MaterialHandle
		CreatePbrMaterial(const PbrMaterialDesc& desc) = 0;

		/**
		 * Creates a loose (unbaked, per-channel) PBR material in this scene's loose-material buffer
		 * and returns a handle referencing it. Bind it like any material; it renders through the same
		 * lighting path as a PbrMaterial. See LoosePbrMaterialDesc.
		 */
		virtual MaterialHandle
		CreateLoosePbrMaterial(const LoosePbrMaterialDesc& desc) = 0;

		/**
		 * Rewrites a material's contents in place, keeping its handle and its slot.
		 *
		 * A submesh stores the material's entry *index*, so every submesh bound to `material` picks
		 * the new textures and factors up with no rebinding -- this is how a texture is swapped on a
		 * live material. The PSO bucket derives from the material's *type*, which an update cannot
		 * change, so pipeline state is unaffected too.
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
		 * slot in the corresponding material buffer.
		 *
		 * A submesh stores the material's slot index, not a generation-checked handle, so a submesh
		 * still bound to a deleted material silently picks up whichever material next takes that
		 * slot. Rebind every submesh using it (SetSubmeshMaterial) before deleting.
		 *
		 * @param material A handle returned by a material-creating method.
		 * @throws SceneError if the handle is invalid, already deleted, or names a material type
		 *         that has no storage to free (kNull, kAssert).
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
		 * Whether `geom` still refers to live geometry in this scene.
		 */
		[[nodiscard]] virtual bool
		IsGeomAlive(GeomHandle geom) const noexcept = 0;

		/**
		 * Removes geometry and frees its underlying vertex/index/meshlet data.
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
