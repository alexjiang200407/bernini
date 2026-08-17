#pragma once

#include <QStringList>
#include <QWidget>

#include "Thumbnails/TexturePreviewCache.h"
#include "Windows/ContentExplorer/AssetFileModel.h"
#include "Windows/ContentExplorer/AssetOperations.h"

#include "ui_ContentExplorerWindow.h"

class AssetThumbnailCache;
class QAbstractItemView;
class QFileSystemModel;
class QLabel;
class QModelIndex;
class QPoint;

class ContentExplorerWindow : public QWidget
{
	Q_OBJECT

public:
	/**
	 * A function that returns the asset paths that are still open
	 */
	using AssetsHeldOpenFn = AssetOperations::AssetsHeldOpenFn;

	/**
	 * `assetsHeldOpen` has no default because it guards a deletion. An open graph holds a material in
	 * memory, and its next Save would write a deleted one straight back; the Animation panel holds the
	 * mesh and clip files its source dropdown offers, which a deletion would turn into dead entries --
	 * nothing on disk records either, so the reference graph cannot see them and only this can. A guard
	 * that could be left unwired would fail open, silently, and MainWindow is not covered by a test
	 * that would notice.
	 *
	 * A caller with genuinely nothing open says so: `[] { return QStringList(); }`.
	 */
	ContentExplorerWindow(QWidget* parent, AssetsHeldOpenFn assetsHeldOpen);

	/**
	 * Points both views at the given directory and enables the explorer: the tree
	 * shows its sub-folders and the table shows the contents of the selected folder.
	 *
	 * @param path Absolute path to the directory the explorer should be rooted at.
	 */
	void
	SetRootPath(const QString& path);

	// Supplies the grid's thumbnails. Without one the tiles keep their shell icons.
	void
	SetThumbnails(AssetThumbnailCache* thumbnails);

	[[nodiscard]] TexturePreviewCache&
	GetTexturePreviews() noexcept
	{
		return m_TexturePreviews;
	}

Q_SIGNALS:
	/**
	 * A bake rewrote `asset` (data-root-relative) on disk. Anything showing what that file says -- the
	 * Material Editor's properties panel -- has to re-read it; nothing here watches the filesystem.
	 */
	void
	MaterialBaked(const QString& asset);

protected:
	// Keeps the empty-directory placeholder sized to the file table's viewport.
	bool
	eventFilter(QObject* watched, QEvent* event) override;

	void
	dragEnterEvent(QDragEnterEvent* event) override;
	void
	dragMoveEvent(QDragMoveEvent* event) override;
	void
	dropEvent(QDropEvent* event) override;

private:
	void
	AttachModels();

	/**
	 * Hides every build product (.bvat) among `parent`'s rows [`first`, `last`] of `model` in
	 * `view` -- the whole parent when `last` is -1. Row-hiding is per view and resets with the
	 * model attachment, so this runs from rowsInserted (the lazy scan's arrivals, which pass their
	 * batch), from the tree's expanded signal and after each grid re-root (rows already cached in
	 * the model fire neither, and scan whole).
	 */
	static void
	HideBuildProductRows(
		QAbstractItemView*      view,
		const QFileSystemModel& model,
		const QModelIndex&      parent,
		int                     first = 0,
		int                     last  = -1);

	/**
	 * Roots the grid at `path` and moves the tree's selection to match, leaving the history alone.
	 * Every navigation ends here.
	 */
	void
	ShowDirectory(const QString& path);

	/** Shows `path`, remembering the folder it replaces so Back can return to it. */
	void
	NavigateTo(const QString& path);

	/**
	 * Returns to the folder shown before, skipping any that has been deleted since -- a folder can go
	 * while it sits in the history, and there is nothing to show of one that is gone.
	 */
	void
	NavigateBack();

	/** Detaches the models and disables the explorer, leaving both views empty. */
	void
	Clear();

	void
	ShowHierarchyMenu(const QPoint& pos);

	void
	ShowFileMenu(const QPoint& pos);

	/**
	 * The right-click menu both views share: Add Directory, and Rename / Delete / Delete Cascade on an
	 * asset. The tree lists files as well as folders, so an asset can be acted on from either side
	 * without navigating to it first.
	 */
	void
	ShowAssetMenu(QAbstractItemView& view, QFileSystemModel& model, const QPoint& pos);

	/** Re-roots the grid when the folder it is sitting in or under has been deleted. */
	void
	OnDirectoryDeleted(const QString& absolute);

	/** Follows the grid to where the folder it is sitting in or under has been renamed to. */
	void
	OnDirectoryRenamed(const QString& fromAbsolute, const QString& toAbsolute);

	void
	UpdateEmptyPlaceholder();

	Ui::ContentExplorerWindow m_Ui;
	QFileSystemModel*         m_HierarchyModel;
	AssetFileModel*           m_FileModel;
	QLabel*                   m_EmptyPlaceholder = nullptr;
	QString                   m_RootPath;
	QStringList               m_History;
	AssetOperations*          m_Operations = nullptr;

	// A pure-CPU decode with no renderer behind it, so the explorer stands its own cache instead of
	// being handed one the way the GPU-backed AssetThumbnailCache is.
	TexturePreviewCache m_TexturePreviews;
};
