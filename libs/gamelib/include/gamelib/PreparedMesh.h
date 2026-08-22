#pragma once
#include <assetlib/AssetStore.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/IScene.h>
#include <bgl/PreparedStaticMesh.h>
#include <core/str/str.h>
#include <gamelib/ClipInfo.h>

namespace game
{
	/**
	 * The texture files `material` names, relative to the data root: the nine authoring routes when
	 * `loose`, otherwise the baked triplet. Unrouted slots come back as empty strings, so the result
	 * is positional.
	 *
	 * `loose` is `assetlib::drawsLoose` against the data root -- the caller passes the verdict in
	 * rather than it being taken here, so one material is measured against the disk once and every
	 * derived thing agrees with it.
	 *
	 * Public because decoding a texture is expensive and pure CPU, while uploading it is neither --
	 * it must happen on the render thread. A caller that wants the decode off that thread needs to
	 * know what to decode before it acquires anything. See TexturePrefetch.
	 */
	[[nodiscard]] std::vector<std::string>
	MaterialTextures(const assetlib::BMaterial& material, bool loose);

	/**
	 * Textures decoded ahead of time, keyed by the data-root-relative path they will be asked for.
	 *
	 * `assetlib::loadKTX2` transcodes a whole Basis mip chain and is the dominant cost of loading a
	 * material -- but it touches no GPU state, so it can run anywhere, unlike the upload that follows.
	 * Hand one of these to AcquireTexture / AcquireMaterial and a matching entry is consumed instead
	 * of the file being read, leaving only the upload on the render thread.
	 *
	 * Entries are moved out as they are used. A supplied prefetch is the whole truth: a path it does
	 * not carry resolves to the scene's default map (with a warning), never to a read of the file --
	 * so handing one in *is* the guarantee that the acquiring thread does no decode. A texture whose
	 * decode failed is simply left out, and was reported where it failed.
	 */
	using TexturePrefetch = core::str::unordered_str_map<assetlib::ImageData>;

	/** Which AssetManager door a PreparedMesh was made for. An acquire refuses another tier's. */
	enum class MeshTier
	{
		kStatic,
		kVat,
		kSkinned,
	};

	/** One material a prepared mesh names, read and measured against the disk ahead of the upload. */
	struct PreparedMaterial
	{
		// Empty for a slot the mesh names nothing in; the acquire leaves that submesh unlit.
		std::string relPath;

		assetlib::BMaterial source;

		// assetlib::drawsLoose, decided where the file was read. Carried rather than re-measured so
		// the acquire touches no disk -- and so one material is measured once, as CreateMaterial
		// already requires.
		bool loose = false;
	};

	/**
	 * A mesh acquire's off-thread half: every file read, the meshlet cook, every texture decode and
	 * -- on the animated tiers -- the bake or the pose measurement, already done. What is left is
	 * upload, which is what lets the render thread hold nothing but the commit.
	 *
	 * Move-only and single-spend: `cooked` is consumed by the acquire that takes it, and a second
	 * acquire of the same payload is refused.
	 *
	 * It carries no `assetlib::BMesh`. Everything the commit reads off one -- the material each
	 * submesh names, the submesh count -- is flattened here, so the parsed container is dropped on
	 * the thread that read it rather than travelling to the render thread to be dropped there.
	 */
	struct PreparedMesh
	{
		// Move-only, because the cook is. Said out loud rather than left implicit: the build is
		// /Wall /WX, where an implicitly deleted copy is a warning.
		PreparedMesh()  = default;
		~PreparedMesh() = default;

		PreparedMesh(PreparedMesh&&) = default;

		PreparedMesh&
		operator=(PreparedMesh&&) = default;

		PreparedMesh(const PreparedMesh&) = delete;

		PreparedMesh&
		operator=(const PreparedMesh&) = delete;

		MeshTier tier = MeshTier::kStatic;

		// Normalized, and what the acquire keys the shared geom by.
		std::string relPath;
		uint32_t    meshIndex = 0;

		// kVat / kSkinned: the normalized `.banim` this was prepared against.
		std::string animations;

		bgl::PreparedStaticMesh cooked;

		// Parallel to the source `.bmesh`'s material list -- the shape AddStaticMeshGeom's
		// `materials` span wants, so the acquire resolves handles into it by index.
		std::vector<PreparedMaterial> materials;

		// One entry per submesh of `meshIndex`: the index into `materials` it binds to. Out of
		// range names no material, which the static tier draws unlit and the animated tiers refuse.
		std::vector<uint32_t> submeshMaterials;

		TexturePrefetch textures;

		// kSkinned only. `posedBounds` is the box the geom culls by, read off the `.banim`'s bake or
		// measured here -- never on the render thread, where measuring is seconds on a dense rig.
		assetlib::Skeleton     skeleton;
		assetlib::AnimationSet clips;
		assetlib::Bounds       posedBounds;

		// kVat only: the geom's desc less the two texture handles the acquire uploads and fills in,
		// and the decoded pair they come from -- the single largest cost on the VAT path.
		bgl::VatGeomDesc      vatDesc;
		std::vector<ClipInfo> vatClips;
		assetlib::ImageData   vatPositions;
		assetlib::ImageData   vatNormals;

		// What the pair is shared by: the container plus the bake it holds, so two meshes of one
		// `.bvat` share the uploads and a re-bake from another `.banim` cannot inherit old pixels.
		std::string vatPositionsKey;
		std::string vatNormalsKey;
	};

	/**
	 * Everything AssetManager::AcquireMesh does before it touches the scene: the `.bmesh` read, the
	 * meshlet cook, and the read and texture decode of every material its submeshes name.
	 *
	 * Pure assetlib and `bgl::CookStaticMesh`, so it may run on any thread -- which is the point.
	 * It reads `store` and mutates nothing, so it is safe to run while another thread acquires
	 * through a manager over the same store, provided nothing writes to the store meanwhile.
	 *
	 * It cannot consult the manager's cache from here, so a mesh that is already live is prepared
	 * anyway and the work is dropped by the acquire. Call the path-taking acquire when the caller
	 * is not on a thread it minds blocking.
	 *
	 * @throws std::runtime_error if the mesh cannot be read or `meshIndex` is out of range;
	 *         bgl::SceneError for anything CookStaticMesh refuses.
	 */
	[[nodiscard]] PreparedMesh
	PrepareMesh(
		const assetlib::AssetStore& store,
		std::string_view            relPath,
		uint32_t                    meshIndex = 0);

	/**
	 * PrepareMesh for the VAT tier, plus what that tier reads: the pair's `.bvat` made fresh
	 * (EnsureVatBaked -- seconds of CPU skinning when it is stale) and its position/normal textures
	 * decoded.
	 *
	 * @throws std::runtime_error if an input cannot be read, the bake refuses the pair, or the
	 *         `.bvat` does not cover the mesh's submeshes; bgl::SceneError for anything
	 *         CookStaticMesh refuses.
	 */
	[[nodiscard]] PreparedMesh
	PrepareVatMesh(
		const assetlib::AssetStore& store,
		std::string_view            relPath,
		std::string_view            animationsRelPath,
		uint32_t                    meshIndex = 0);

	/**
	 * PrepareMesh for the skinned tier, plus what that tier reads: the clip set, the skeleton it
	 * names, the signature check between them, and the posed box.
	 *
	 * `posedBounds` follows AcquireSkinnedMesh's rule -- given, it is used; absent, the `.banim`'s
	 * baked box is read, and only a pairing the cook never measured is measured here.
	 *
	 * @throws std::runtime_error if an input cannot be read, `meshIndex` is out of range, or the
	 *         clip set no longer matches the skeleton it names; bgl::SceneError for anything
	 *         CookStaticMesh refuses.
	 */
	[[nodiscard]] PreparedMesh
	PrepareSkinnedMesh(
		const assetlib::AssetStore&            store,
		std::string_view                       relPath,
		std::string_view                       animationsRelPath,
		uint32_t                               meshIndex   = 0,
		const std::optional<assetlib::Bounds>& posedBounds = std::nullopt);
}
