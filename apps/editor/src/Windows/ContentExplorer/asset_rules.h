#pragma once

#include <QString>
#include <QStringList>

class QFileSystemModel;
class QModelIndex;

namespace editor
{
	/**
	 * The data-root-relative path of the thing at `index` that may be acted on -- an asset file, an
	 * imported source, or a directory the project is not scaffolded with. Empty for a file of no kind
	 * the project tracks, for a source's `.bimport` sidecar, for one of the scaffolded directories,
	 * for anything outside the data root, or for no row at all.
	 *
	 * `dataRoot` is what the explorer is rooted at. Free of the window because it is the one thing a
	 * delete cannot afford to get wrong, and a QMenu cannot be driven from a test.
	 */
	[[nodiscard]] QString
	AssetAt(const QFileSystemModel& model, const QModelIndex& index, const QString& dataRoot);

	/**
	 * Whether `asset` (a data-root-relative path) is a material, and so gets a Bake action. By the
	 * extension alone, like AssetAt -- a `.bmaterial` is the only thing baking has anything to do.
	 */
	[[nodiscard]] bool
	IsMaterialAsset(const QString& asset);

	/**
	 * Whether `asset` (a data-root-relative path) is one a person may rename or delete.
	 *
	 * False for anything under `Derived/`: a bake's to write and to regenerate, never anyone's to
	 * name. The views no longer reach those files at all, which is what makes this unreachable in
	 * practice -- and exactly why it is written down. A rule that holds only because of where a
	 * view happens to be rooted is one the next panel breaks without noticing, and this one is the
	 * difference between a stale container and lost work.
	 *
	 * By the location, through `assetlib::originOf`, rather than by extension: the half a file
	 * lives in is what the library's own invariant is stated on.
	 */
	[[nodiscard]] bool
	IsActionableAsset(const QString& asset);

	/**
	 * Whether `asset` is a thing a person may delete, as opposed to merely rename.
	 *
	 * False for an imported source. `assetTypeFromExtension` does not know a `.glb`, so
	 * `planDeletion` throws for one rather than planning it -- and grouped deletion is ADR-8's
	 * explicit non-goal, because deleting a source that shares its rig with another would have to
	 * either strand that rig or take it. A rename rewrites where a deletion destroys, which is why
	 * the same group is safe to move and not safe to remove.
	 */
	[[nodiscard]] bool
	IsRemovableAsset(const QString& asset);

	/** What the Content Explorer is browsing. Ordered to match `ModeSelector`'s items. */
	enum class BrowseMode
	{
		kAssets,
		kTextures
	};

	/**
	 * Where the views root for `mode`, under the data root at `dataRoot`. Empty for an empty root.
	 *
	 * A mode is a browse root and nothing else, which is what keeps `AssetFileModel` a plain
	 * `QFileSystemModel`: the alternative is a proxy standing between every view and the model to
	 * present one directory as another. Everything else -- what a path *means*, what an operation
	 * resolves against -- stays on the data root either way.
	 */
	[[nodiscard]] QString
	BrowseRootFor(const QString& dataRoot, BrowseMode mode);

	/**
	 * Whether `mode` may be written into at all: a new directory, an import dropped from outside, a
	 * rename, a delete.
	 *
	 * Textures are derived, and ADR-6 says a person never edits those. `IsActionableAsset` already
	 * refuses each one file at a time; this refuses the gestures that name no file, which are the
	 * ones it cannot see.
	 */
	[[nodiscard]] bool
	IsEditableMode(BrowseMode mode);

	/**
	 * Whether any of `heldOpen` is `absolute`, or -- when that names a directory -- something
	 * beneath it. `heldOpen` is what the panels have open, absolute or relative to the working
	 * directory: a viewport's environment comes out of config.json relative, and a path compared
	 * as a location without being resolved first silently matches nothing.
	 *
	 * Free of the operation for the reason AssetAt is: every dialog around it is modal.
	 */
	[[nodiscard]] bool
	IsHeldOpen(const QStringList& heldOpen, const QString& absolute, bool isDirectory);

	/**
	 * Whether `name` can be the name of an asset or folder: one path component, usable on every
	 * platform a project is shared between. Windows refuses `<>:"/\|?*`, control characters and the
	 * DOS device names (`NUL`, `COM1`, ... -- with or without an extension), and silently strips a
	 * trailing dot or space; a leading dot hides the file elsewhere. Any of these would make a name
	 * the project cannot round-trip.
	 */
	[[nodiscard]] bool
	IsValidAssetFileName(const QString& name);
}
