#pragma once

#include <QString>

namespace editor
{
	/**
	 * Writes the empty avatar -- `{ "legs": [] }` -- for the rig at `skeletonKey`, at the key
	 * `avatarKeyFor` names, through the project's store. Returns that key.
	 *
	 * Empty rather than guessed: the legs are pack conventions, and a heuristic that named them
	 * would be a silent mis-solve the first time a pack disagreed. The document exists so that
	 * whoever fills it in never has to know the convention that finds it.
	 *
	 * Free of the window for the reason `AssetAt` is: the one thing the action cannot afford to get
	 * wrong is the path, and a QMenu cannot be driven from a test.
	 *
	 * @param dataRoot     The project's Data directory.
	 * @param skeletonKey  A `.bskel` under `Derived/Skeletons`, data-root-relative.
	 * @throws std::runtime_error if an avatar already stands at that key -- it is authored work,
	 *         and an empty one over it would be the loss this whole layout exists to prevent -- or
	 *         for anything `avatarKeyFor` and `AssetStore::Save` refuse.
	 */
	[[nodiscard]] QString
	CreateEmptyAvatar(const QString& dataRoot, const QString& skeletonKey);
}
