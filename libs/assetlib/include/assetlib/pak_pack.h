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

	/**
	 * Walks a data root and writes one `.bpak` of everything the runtime reads.
	 *
	 * What goes in is `assetTypeFromExtension`, so a new container type joins the archive by being
	 * registered once, plus two explicit exclusions: a `textures_src` or `meshes_src` component is
	 * authoring source, and the import document is authored state a read-only store never reads.
	 * Anything else unregistered is counted in `skippedByExtension` rather than dropped silently.
	 *
	 * Nothing stale enters the archive, which is the whole of a read-only store's trust:
	 *
	 * - Geometry (`.bmesh`, `.bskel`, `.banim`) carries what the regeneration seam answers -- a
	 *   stale group re-bakes *into the archive* (the file on disk is `migrate`'s, and stays), and
	 *   a binding-only document edit is baked in with no `migrate` run.
	 * - A stale `.bvat` -- its own stamps, or a geometry group that is a cache miss -- is re-baked
	 *   on disk first, from the seam's outputs. Every packed `.bvat` is then re-stamped against
	 *   the geometry *as archived*, so a staleness question asked inside the archive answers
	 *   fresh.
	 *
	 * `target` is untouched until the archive is whole: `PakWriter` streams to a temp and renames.
	 *
	 * @throws std::runtime_error if an asset cannot be read, if a stale `.bvat` cannot be re-baked,
	 *         if a geometry group cannot be served (no source to regenerate a stale entry from, or
	 *         a binding naming a submesh the mesh does not have), or if the archive cannot be
	 *         written.
	 */
	[[nodiscard]] PackReport
	packProject(const AssetStore& store, const PackDesc& desc);
}
