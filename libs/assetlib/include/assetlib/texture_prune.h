#pragma once
#include <assetlib_structs/BMaterial.h>

namespace assetlib
{
	/**
	 * Where a prune looks for the materials that keep a baked map alive, and for the maps themselves.
	 *
	 * Mirrors MaterialBakeDesc: `textureDir` is the directory bakeMaterial writes its output into,
	 * relative to `dataRoot`. Prune the same pair you baked with, or the sweep will not find the maps.
	 */
	struct TexturePruneDesc
	{
		std::filesystem::path dataRoot;  // the project's Data directory
		std::filesystem::path textureDir = "Textures";
	};

	struct UnusedTexture
	{
		std::string path;  // relative to dataRoot, e.g. "Textures/orm_fdc537ad982f59e7.ktx2"
		uint64_t    bytes = 0;
	};

	struct TexturePruneScan
	{
		std::vector<UnusedTexture> unused;

		size_t   materialsScanned = 0;
		size_t   liveMaps         = 0;  // distinct baked maps some material still names
		size_t   candidates       = 0;  // baked maps present in the texture directory
		uint64_t bytes            = 0;  // total size of `unused`
	};

	/**
	 * Finds the baked maps under `desc.textureDir` that no material references any more, without
	 * deleting anything. A re-bake whose routes changed writes a new content-hashed file and leaves the
	 * old one behind, so these accumulate; this is how they are found.
	 *
	 * Mark and sweep. **Mark**: every `.bmaterial` below `dataRoot` is loaded and the three entries of
	 * its baked triplet are taken as live -- regardless of the material's `mode`, because a kLoose
	 * material still carries the triplet of its last bake and deleting those maps would destroy a valid
	 * bake. **Sweep**: everything in the texture directory that `bakeMaterial` could have written (see
	 * isBakedMapName) and that nothing marked is reported as unused. A hand-placed map such as
	 * `skybox.ktx2` or `brdf_lut.ktx2` never matches the bake's naming and so is never a candidate.
	 *
	 * A missing texture directory is not an error -- nothing has been baked, so nothing is unused.
	 *
	 * @throws std::runtime_error if `dataRoot` does not exist, or if any `.bmaterial` below it cannot be
	 *         read. An unreadable material is fatal on purpose: its references would silently go
	 *         unmarked, and the maps it alone keeps alive would be swept as garbage.
	 */
	[[nodiscard]] TexturePruneScan
	findUnusedBakedTextures(const TexturePruneDesc& desc);

	struct TexturePruneResult
	{
		size_t                   deleted = 0;
		uint64_t                 bytes   = 0;  // reclaimed by the files actually removed
		std::vector<std::string> failed;       // could not be removed; still on disk
	};

	/**
	 * Deletes the maps a scan reported as unused. Split from the scan so a caller can show what it is
	 * about to destroy and take a confirmation first.
	 *
	 * A file that has vanished since the scan counts as deleted, not as a failure. A file that cannot be
	 * removed is collected into `failed` rather than throwing, so one locked map does not abandon the
	 * rest of the sweep.
	 *
	 * Pass the same `desc` the scan was taken with: the paths are relative to its `dataRoot`. Re-baking
	 * or editing a material between the scan and this call invalidates the scan -- take a fresh one.
	 */
	TexturePruneResult
	deleteUnusedBakedTextures(const TexturePruneScan& scan, const TexturePruneDesc& desc);
}
