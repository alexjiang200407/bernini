#pragma once

#include <QStringList>
#include <QWidget>

#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>

#include "Windows/ContentExplorer/AssetFileModel.h"

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
	using AssetsHeldOpenFn = std::function<QStringList()>;

	/**
	 * `assetsHeldOpen` has no default because it guards a deletion. An open graph holds a material in
	 * memory, and its next Save would write a deleted one straight back -- nothing on disk records that,
	 * so the reference graph cannot see it and only this can. A guard that could be left unwired would
	 * fail open, silently, and MainWindow is not covered by a test that would notice.
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

	/**
	 * The data-root-relative path of the thing at `index` that may be deleted -- an asset file, or a
	 * directory the project is not scaffolded with. Empty for a file of no kind the project tracks, for
	 * one of the scaffolded directories, for anything outside the data root, or for no row at all.
	 *
	 * `dataRoot` is what the explorer is rooted at. Lifted out of the menu handler because it is the one
	 * thing a delete cannot afford to get wrong, and a QMenu cannot be driven from a test.
	 */
	[[nodiscard]] static QString
	AssetAt(const QFileSystemModel& model, const QModelIndex& index, const QString& dataRoot);

	/**
	 * Whether `asset` (a data-root-relative path) is a material, and so gets a Bake action. By the
	 * extension alone, like AssetAt -- a `.bmaterial` is the only thing baking has anything to do.
	 */
	[[nodiscard]] static bool
	IsMaterialAsset(const QString& asset);

	/**
	 * Derives a `.bmaterial` from every PBR material `imported` carries, writes it under `materialDir`,
	 * and points each submesh cut from it at the file (relative to `dataRoot`, like every asset
	 * reference). Each is routed at the `texN.ktx2` files `writeTextures` puts in `textureDir`.
	 *
	 * Non-PBR materials are skipped, leaving their submeshes unassigned -- which both runtimes already
	 * render unlit. Every file is written before any submesh is pointed at one, so a failure part-way
	 * leaves a mesh naming only materials that exist.
	 *
	 * Public, and taking everything it needs, because the import that calls it cannot be driven from a
	 * test -- it is behind a modal dialog -- and this is the half worth pinning.
	 *
	 * @throws std::runtime_error if a file cannot be written.
	 */
	static void
	WriteImportedMaterials(
		const assetlib::imp::BMeshImport& imported,
		assetlib::BMesh&                  mesh,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      materialDir,
		const std::filesystem::path&      textureDir);

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

	QString
	ResolveDropDirectory(const QPoint& windowPos) const;

	/** What became of an import, so a multi-file drop knows whether to carry on with the next one. */
	enum class ImportOutcome
	{
		kImported,
		kCancelled,  // by the user, at the overwrite prompt or on the loading screen
		kFailed,     // already reported to the user
	};

	/** What the import dialog asked for. */
	struct ImportOptions
	{
		QString textureSubdir;      // folder under c_TextureRoot; empty skips texture extraction
		bool pbrMaterials = false;  // ignored without textureSubdir -- a material routes at those
	};

	/**
	 * Converts a dropped glTF/glb into the engine .bmesh format written to `targetDir`.
	 *
	 * Parsing and supercompressing the textures run on a worker thread behind a cancellable loading
	 * screen: they take long enough to freeze the editor. Nothing there touches bgl. The material
	 * graphs are built afterwards, back on the UI thread -- their nodes own QPixmaps, which belong to
	 * it -- so the `.bmesh` is written from the UI thread too, once its materials exist to be named.
	 *
	 * Asks before overwriting anything, reports a failure to the user, and on either a failure or a
	 * cancel removes the half-written files it had produced -- see RollBack.
	 */
	[[nodiscard]] ImportOutcome
	ImportMesh(const QString& sourceFile, const QString& targetDir, const ImportOptions& options);

	/** A directory an import writes into, and whether the import is the one that made it. */
	struct ImportedDir
	{
		std::filesystem::path path;             // empty when the import writes no such directory
		bool                  existed = false;  // whether it was there before the import started
		std::string_view      categoryRoot;  // the category it sits under, never itself removable
	};

	/**
	 * Deletes what an import got as far as writing, so a cancelled or failed one leaves nothing behind:
	 * a `.bmesh` naming textures that were never extracted, or a half-supercompressed texture folder.
	 *
	 * Only what the import itself created, and never anything that was already there -- a texture folder
	 * that predates the import is left alone, files and all, because the user was asked before it was
	 * written into and its other contents are not ours to delete.
	 */
	static void
	RollBack(
		const std::filesystem::path& bmeshPath,
		bool                         bmeshExisted,
		std::span<const ImportedDir> dirs);

	/** Detaches the models and disables the explorer, leaving both views empty. */
	void
	Clear();

	void
	ShowHierarchyMenu(const QPoint& pos);

	void
	ShowFileMenu(const QPoint& pos);

	/**
	 * The right-click menu both views share: Add Directory, and Delete on an asset. The tree lists files
	 * as well as folders, so an asset can be deleted from either side without navigating to it first.
	 */
	void
	ShowAssetMenu(QAbstractItemView& view, QFileSystemModel& model, const QPoint& pos);

	/**
	 * Deletes `asset` (data-root-relative), having first established that nothing references it: no
	 * material samples the texture, no mesh names the material.
	 *
	 * Deleting a mesh is never refused and never cascades -- the materials it named are shareable assets
	 * and stay where they are. The maps a deleted material leaves behind are what Clean Unused Textures
	 * sweeps.
	 */
	void
	DeleteAsset(const QString& asset);

	/**
	 * Composites the material at `asset` (data-root-relative) down to its baked triplet and rewrites it,
	 * on the loading-screen worker. Reads the material off disk, so it bakes the routes last saved --
	 * see the Material Editor's Save.
	 */
	void
	BakeMaterial(const QString& asset);

	/**
	 * `parentPath` rather than a QModelIndex: this runs a modal below, and QFileSystemModel populates on
	 * a worker whose row insertions invalidate every index into it. The index is re-derived from the
	 * path once the dialog is down.
	 */
	void
	AddDirectory(QFileSystemModel* model, const QString& parentPath);

	void
	UpdateEmptyPlaceholder();

	Ui::ContentExplorerWindow m_Ui;
	QFileSystemModel*         m_HierarchyModel;
	AssetFileModel*           m_FileModel;
	QLabel*                   m_EmptyPlaceholder = nullptr;
	QString                   m_RootPath;
	AssetsHeldOpenFn          m_AssetsHeldOpen;
};
