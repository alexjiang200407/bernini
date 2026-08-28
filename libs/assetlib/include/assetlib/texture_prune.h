#pragma once
#include <assetlib/project_layout.h>

namespace assetlib
{
	/**
	 * Where a prune looks for the materials that keep a baked map alive, and for the maps themselves.
	 *
	 * `textureDir` is the directory the scan walks, defaulting to the one every bake writes into,
	 * relative to `dataRoot`. Prune the same pair you baked with, or the sweep will not find the maps.
	 */
	struct TexturePruneDesc
	{
		std::filesystem::path textureDir = c_BakedTexturesDirectoryName;
	};

	struct UnusedTexture
	{
		std::string path;  // relative to dataRoot, e.g. "Derived/BakedTextures/orm_fdc537ad9.ktx2"
		uint64_t    bytes = 0;
	};

	struct TexturePruneScan
	{
		std::vector<UnusedTexture> unused;

		size_t   materialsScanned    = 0;
		size_t   environmentsScanned = 0;  // `.bsky` + `.benvl` assets read by the mark phase
		size_t   liveMaps            = 0;  // distinct baked maps some asset still names
		size_t   candidates          = 0;  // baked maps present in the texture directory
		uint64_t bytes               = 0;  // total size of `unused`
	};

	struct TexturePruneResult
	{
		size_t                   deleted = 0;
		uint64_t                 bytes   = 0;  // reclaimed by the files actually removed
		std::vector<std::string> failed;       // could not be removed; still on disk
	};
}
