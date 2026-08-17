#pragma once
#include <assetlib/AssetStore.h>
#include <core/str/str.h>

namespace assetlib
{
	/** What `packProject` put in the archive, and what it left out. */
	struct PackReport
	{
		uint32_t entries      = 0;
		uint64_t payloadBytes = 0;

		/** `.bvat` files found stale and re-baked before packing. See packProject. */
		uint32_t vatsRebaked = 0;

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

	/**
	 * Walks a data root and writes one `.bpak` of everything the runtime reads.
	 *
	 * What goes in is `assetTypeFromExtension`, so a new container type joins the archive by being
	 * registered once, plus one explicit exclusion: a `textures_src` component is authoring source.
	 * Anything else unregistered is counted in `skippedByExtension` rather than dropped silently.
	 *
	 * A stale `.bvat` is re-baked first, so what ships is correct by construction -- which is what
	 * lets a read-only mount trust one rather than ask whether it is stale.
	 *
	 * `target` is untouched until the archive is whole: `PakWriter` streams to a temp and renames.
	 *
	 * @throws std::runtime_error if an asset cannot be read, if a stale `.bvat` cannot be re-baked,
	 *         or if the archive cannot be written.
	 */
	[[nodiscard]] PackReport
	packProject(const AssetStore& store, const PackDesc& desc);
}
