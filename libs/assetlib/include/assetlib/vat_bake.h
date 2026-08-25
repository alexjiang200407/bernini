#pragma once
#include <assetlib/AssetStore.h>

namespace assetlib
{
	struct AnimationSet;
	struct BMesh;
	struct BVat;
	struct Skeleton;

	/**
	 * Bakes a rig's clips into the VAT texture pair: every vertex of every submesh CPU-skinned at
	 * every frame of every clip (see skinSubmesh -- this is that reference path, run to exhaustion),
	 * positions unorm-packed in one AABB closed over all of it, clips stacked along V with a
	 * duplicated terminal row each. The input paths and stamps are left empty; the overload below
	 * fills them.
	 *
	 * @throws std::runtime_error if no submesh of `mesh` carries joint indices, if `animations` was
	 *         cooked against a different rig than `skeleton` (by signature), if the clip set is
	 *         empty or malformed, or if the vertex columns or padded frame rows exceed what a
	 *         texture can hold (c_MaxVatTextureDim), naming the count that broke it.
	 */
	[[nodiscard]] BVat
	bakeVat(const BMesh& mesh, const Skeleton& skeleton, const AnimationSet& animations);

	/** A bake addressed the way every reference in a project is: as keys into a store. */
	struct VatBakeDesc
	{
		std::string mesh;        // a .bmesh
		std::string animations;  // a .banim
	};

	/**
	 * The path form the bake records in the container: lexically normal, generic separators.
	 * Compare a requested path against a `BVat`'s recorded one through this, never raw.
	 */
	[[nodiscard]] std::string
	normalizePath(std::string_view path);

	/**
	 * Where the bake of `meshRelPath` + `animationsRelPath` lives: beside the mesh, named
	 * `<mesh>@<clips>-<hash>.bvat` -- one file per (mesh, clip set), so switching clip sets never
	 * re-bakes what an earlier switch already paid for. The hash is core::hash_string over the
	 * normalized clip-set path; a collision (or a hand-copied file) falls through the recorded-path
	 * check in the freshness rule and re-bakes, never loads wrong clips. renameAsset moves a bake
	 * here when a rename changes its derived name, which is what keeps a rename a load, not a bake.
	 */
	[[nodiscard]] std::filesystem::path
	vatPathFor(std::string_view meshRelPath, std::string_view animationsRelPath);

	/**
	 * Everything but the pixels: the header, the chunk table and the table chunks alone, leaving
	 * `positionsKtx2` / `normalsKtx2` empty. The pixel chunks are the overwhelming bulk of the
	 * file and are never read, so `describe` and any whole-project survey must come through here
	 * rather than `store.Load<BVat>`. (The palettes still load, and they are megabytes on a long clip set --
	 * a scan that wants the references alone wants loadVatRefs.)
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] BVat
	loadVatTables(const std::filesystem::path& path);

	/** The three inputs a `.bvat` was baked from. See loadVatRefs. */
	struct VatRefs
	{
		std::string mesh;
		std::string skeleton;
		std::string animations;
	};

	/**
	 * What `path` was baked from, read seek-only like loadVatTables -- the reference scan's entry
	 * point.
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] VatRefs
	loadVatRefs(const std::filesystem::path& path);

}
