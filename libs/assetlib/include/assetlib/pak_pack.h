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
		 * Extensions found under the data root that no asset type claims, and how many of each.
		 *
		 * Reported rather than dropped in silence. The exclusion rule is derived from
		 * `assetTypeFromExtension`, so a runtime container nobody registered there is absent from
		 * every archive with nothing said about it -- this is what makes that visible.
		 */
		core::str::unordered_str_map<uint32_t> skippedByExtension;

		/**
		 * Packed materials that draw from their authoring routes rather than a baked triplet.
		 *
		 * An archive carries no `textures_src`, so these have nothing to sample once they ship: the
		 * exclusion rule is right and the material is simply not baked yet. Reported because the
		 * archive is otherwise valid -- every entry reads back, and the failure only appears as an
		 * untextured surface on whatever machine opens it.
		 *
		 * Sorted, so that two packs of one tree report the same thing: the walk itself is in
		 * directory-iteration order, which is not the same on two filesystems.
		 */
		std::vector<std::string> materialsDrawingLoose;
	};

	/**
	 * What `pack` writes and the editor looks for, beside the data root rather than inside it: an
	 * archive of a tree is not a member of it, and one packed into the tree it came from would be a
	 * candidate for the next pack.
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
	 * What goes in is derived from `assetTypeFromExtension` rather than from a list kept here, so a
	 * new container type joins the archive by being registered once. On top of that, one explicit
	 * exclusion: any path with a `textures_src` component is authoring source, which the bake reads
	 * and the runtime never does. Everything else without a registered extension -- the
	 * `.berniniproject`, a `.glb` awaiting import, the shader cache -- falls out of the same rule and
	 * is counted in `skippedByExtension`.
	 *
	 * A stale `.bvat` is re-baked before it is packed. It is a derived build product whose inputs
	 * may be packed beside it with no writable place to regenerate it, so what ships has to be
	 * correct by construction: this is what lets a read-only mount trust a `.bvat` rather than ask
	 * whether it is stale.
	 *
	 * Nothing at `target` is disturbed until the archive is complete -- `PakWriter` streams to a
	 * temp beside it and renames, so an interrupted pack leaves the previous archive intact rather
	 * than a shipped artifact missing half the project.
	 *
	 * @throws std::runtime_error if `dataRoot` is not a directory, if an asset cannot be read, if a
	 *         stale `.bvat` cannot be re-baked, or if the archive cannot be written.
	 */
	[[nodiscard]] PackReport
	packProject(const AssetStore& store, const PackDesc& desc);
}
