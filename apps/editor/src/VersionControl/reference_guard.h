#pragma once

namespace editor
{
	/** An asset an update would delete, and something staying behind that still needs it. */
	struct AssetStillInUse
	{
		std::filesystem::path asset;
		std::filesystem::path neededBy;
	};

	/**
	 * Which of `deleted` the project cannot afford to lose, because something that will still be
	 * there references them.
	 *
	 * Two people can each be right and still break the project between them: one deletes a texture
	 * nothing of theirs uses any more, while the other has a material that routes from it. Neither
	 * side is wrong on its own, and only the tree they would produce together is.
	 *
	 * A referrer that `deleted` also removes holds nothing back -- both ends go the same way. Nor
	 * does a derived bake: a `.bvat` names the mesh, skeleton and clips it was made from, but one
	 * that outlived them is stale rather than broken, and is remade on demand. What holds an asset
	 * back is `assetlib::planDeletion`'s to say, and this asks it rather than deciding again.
	 *
	 * This is a rule about assets and not about any backend, so it lives here rather than inside one:
	 * a second IVersionControl calls it, and does not reimplement it.
	 *
	 * @param dataDirectory the project's data root, which every reference is resolved against.
	 * @param deleted absolute paths the update would remove; anything outside `dataDirectory` is not
	 *        an asset and is ignored.
	 * @throws std::runtime_error if the project's assets cannot be read, since references nobody can
	 *         see are references an update would delete through.
	 */
	[[nodiscard]] std::vector<AssetStillInUse>
	FindAssetsStillInUse(
		const std::filesystem::path&              dataDirectory,
		const std::vector<std::filesystem::path>& deleted);
}
