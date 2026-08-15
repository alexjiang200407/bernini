#pragma once

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

	/** A bake addressed the way every reference in a project is: relative to the data root. */
	struct VatBakeDesc
	{
		std::filesystem::path dataRoot;
		std::string           mesh;        // a .bmesh, relative to dataRoot
		std::string           animations;  // a .banim, relative to dataRoot
	};

	/**
	 * bakeVat over files: loads the mesh, the skeleton it names and the clip set, bakes, and
	 * records the three paths and their SourceStamps -- what vatIsStale later compares. Writing the
	 * result is the caller's (see saveVat): a `.bvat` is a derived build product, and where it
	 * lands is the caller's convention, not this function's.
	 *
	 * @throws std::runtime_error if an input cannot be read, if the mesh names no skeleton, or for
	 *         anything the in-memory overload refuses.
	 */
	[[nodiscard]] BVat
	bakeVat(const VatBakeDesc& desc);

	/**
	 * Whether any of the three inputs `vat` was baked from has changed -- or gone -- since, by
	 * SourceStamp. A stale `.bvat` is re-baked, never an error: it is wholly derived, and seconds
	 * of CPU skinning away from fresh.
	 */
	[[nodiscard]] bool
	vatIsStale(const BVat& vat, const std::filesystem::path& dataRoot);

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
}
