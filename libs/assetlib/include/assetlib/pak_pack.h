#pragma once
#include <assetlib/AssetStore.h>
#include <core/str/str.h>

namespace assetlib
{
	/** What `AssetStore::Pack` put in the archive, and what it left out. */
	struct PackReport
	{
		uint32_t entries      = 0;
		uint64_t payloadBytes = 0;

		/** `.bvat` files found stale and re-baked before packing. See `AssetStore::Pack`. */
		uint32_t vatsRebaked = 0;

		/** `.bsky` / `.benvl` files whose routed source moved, re-baked before packing. */
		uint32_t envsRebaked = 0;

		/**
		 * Geometry entries whose archived bytes differ from the file on disk -- a stale group
		 * re-baked into the archive, or a rebind baked in.
		 */
		uint32_t geometryRebaked = 0;

		/**
		 * Extensions under the data root that no asset type claims, and how many of each. Counted so
		 * that a runtime container nobody registered in `assetTypeFromExtension` is visible rather
		 * than silently absent from every archive.
		 */
		core::str::unordered_str_map<uint32_t> skippedByExtension;

		/**
		 * Packed materials drawing from their routes rather than a baked triplet. An archive carries
		 * no `textures_src`, so these ship with nothing to sample -- a valid archive whose failure
		 * shows only as an untextured surface. Sorted, so two packs of one tree report alike.
		 */
		std::vector<std::string> materialsDrawingLoose;
	};

	/**
	 * What `pack` writes, beside the data root rather than inside it: an archive packed into the
	 * tree it came from would be a candidate for the next pack.
	 */
	inline constexpr std::string_view c_DefaultArchiveName = "Data.bpak";

	struct PackDesc
	{
		/** The `.bpak` to write. Replaced only once the whole archive is on disk. */
		std::filesystem::path target;
	};
}
