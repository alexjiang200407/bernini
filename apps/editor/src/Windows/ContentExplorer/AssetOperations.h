#pragma once

#include <QObject>
#include <QStringList>

#include <assetlib/asset_refs.h>

class QFileSystemModel;
class QWidget;

/**
 * The on-disk operations the Content Explorer's menu offers: Delete, Delete Cascade, Rename, Bake and
 * Add Directory.
 *
 * Each runs a reference scan or a cook on the loading-screen worker and talks to the user through
 * modal dialogs, so none of it can be driven from a test -- but none of it is a view's business
 * either. What the explorer keeps is the part that is: the two views, and where they are rooted.
 *
 * Nothing here touches a model or an index. Where an operation moves ground out from under a view --
 * a deleted or renamed *directory* the grid is sitting in or under -- it says so by signal and leaves
 * the re-rooting to whoever owns the views.
 */
class AssetOperations : public QObject
{
	Q_OBJECT

public:
	/** A function that returns the asset paths that are still open. */
	using AssetsHeldOpenFn = std::function<QStringList()>;

	/**
	 * `parent` is what every dialog and loading screen is shown over, and the QObject parent.
	 *
	 * `assetsHeldOpen` has no default because it guards a deletion -- see ContentExplorerWindow's
	 * constructor for why that guard cannot be allowed to fail open.
	 */
	AssetOperations(QWidget* parent, AssetsHeldOpenFn assetsHeldOpen);

	/** The data root every asset path below is relative to. Empty disables nothing; it is the caller's
	 *  job not to act on assets while no project is open. */
	void
	SetDataRoot(const QString& dataRoot);

	/**
	 * Deletes `asset` (data-root-relative), having first established that nothing references it: no
	 * material samples the texture, no mesh names the material.
	 *
	 * Deleting a mesh is never refused -- the materials it named are shareable assets. They stay
	 * where they are, and the maps a deleted material leaves behind are what Clean Unused Textures
	 * sweeps.
	 */
	void
	Delete(const QString& asset);

	/**
	 * Delete, taking also every asset that nothing would reference once the target is gone -- listed in
	 * the confirmation first, and blocked by an editor panel holding any of it open.
	 */
	void
	DeleteCascade(const QString& asset);

	/**
	 * Renames `asset` (data-root-relative) in place, rewriting every asset that references it so the
	 * rename never breaks an edge -- which is why it is not blocked by references the way Delete is.
	 * The dialog edits a file's stem alone: the extension says what the asset is, and renaming must
	 * not change that.
	 */
	void
	Rename(const QString& asset);

	/**
	 * Composites the material at `asset` (data-root-relative) down to its baked triplet and rewrites it,
	 * on the loading-screen worker. Reads the material off disk, so it bakes the routes last saved --
	 * see the Material Editor's Save.
	 */
	void
	Bake(const QString& asset);

	/**
	 * Writes the empty avatar for the rig `asset` -- an imported source -- binds its joints to, and
	 * says where by signal so the view can show it. Refused with a dialog when the rig already has
	 * one: an avatar is authored work, and nothing puts one back.
	 */
	void
	CreateAvatar(const QString& asset);

	/**
	 * `parentPath` rather than a QModelIndex: this runs a modal below, and QFileSystemModel populates on
	 * a worker whose row insertions invalidate every index into it. The index is re-derived from the
	 * path once the dialog is down.
	 */
	void
	AddDirectory(QFileSystemModel* model, const QString& parentPath);

Q_SIGNALS:
	/**
	 * A bake rewrote `asset` (data-root-relative) on disk. Anything showing what that file says -- the
	 * Material Editor's properties panel -- has to re-read it; nothing here watches the filesystem.
	 */
	void
	MaterialBaked(const QString& asset);

	/** An avatar was written at `absolute`. The view that offered the action shows it. */
	void
	AvatarCreated(const QString& absolute);

	/** A directory at `absolute` is gone. A view rooted at or inside it now shows nothing. */
	void
	DirectoryDeleted(const QString& absolute);

	/** A directory moved from `fromAbsolute` to `toAbsolute`. A view inside it can follow. */
	void
	DirectoryRenamed(const QString& fromAbsolute, const QString& toAbsolute);

private:
	/**
	 * Whether an editor panel holds `absolute` -- or, when it is a directory, anything beneath it --
	 * open. What every on-disk operation here is gated on, because an open graph's next Save would
	 * write the old state straight back, and a viewport lit by a `.benv` is still drawing it.
	 */
	[[nodiscard]] bool
	IsHeldOpen(const QString& absolute, bool isDirectory) const;

	/** The body Delete and Delete Cascade share; `planner` is the one thing they differ by. */
	void
	DeleteWithPlanner(
		const QString& asset,
		assetlib::DeletionPlan (*planner)(const assetlib::AssetRefGraph&, std::string_view));

	QWidget*         m_Parent;
	QString          m_DataRoot;
	AssetsHeldOpenFn m_AssetsHeldOpen;
};
