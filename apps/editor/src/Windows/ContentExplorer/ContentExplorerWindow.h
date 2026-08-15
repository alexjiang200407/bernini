#pragma once

#include <QStringList>
#include <QWidget>

#include <assetlib/asset_refs.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>

#include "Thumbnails/TexturePreviewCache.h"
#include "Windows/AssetImporter/AssetImporterDialog.h"
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
	 * Whether `name` can be the name of an asset or folder: one path component, usable on every
	 * platform a project is shared between. Windows refuses `<>:"/\|?*`, control characters and the
	 * DOS device names (`NUL`, `COM1`, ... -- with or without an extension), and silently strips a
	 * trailing dot or space; a leading dot hides the file elsewhere. Any of these would make a name
	 * the project cannot round-trip.
	 *
	 * `static` for the reason AssetAt is: the dialog that collects the name cannot be driven from a
	 * test, and this is the rule worth pinning.
	 */
	[[nodiscard]] static bool
	IsValidAssetFileName(const QString& name);

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
	 * @param stems The file stem to write each material under, index-aligned with
	 *        `imported.materials` and empty where no file is wanted. The dialog shows these before the
	 *        import runs, so they are given rather than derived -- deriving them here as well is how a
	 *        preview and a file come to disagree. See editor::MaterialStems for the defaults.
	 * @throws std::runtime_error if a file cannot be written, or if `stems` does not cover
	 *         `imported.materials` -- which means the source was rewritten between the probe that
	 *         named them and the parse that produced these materials.
	 */
	static void
	WriteImportedMaterials(
		const assetlib::imp::BMeshImport& imported,
		assetlib::BMesh&                  mesh,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      materialDir,
		const std::filesystem::path&      textureDir,
		std::span<const QString>          stems);

	/**
	 * Writes the rig a skinned import carries -- the `.bskel` always, the `.banim` only when asked --
	 * and points `mesh` at the skeleton by a data-root-relative path.
	 *
	 * The skeleton is not optional and is deliberately not behind the dialog's checkbox. A joint index
	 * is a bare number into a bone array, so a mesh carrying joints while naming no skeleton is one
	 * `assetlib::save` refuses outright; the clips are the half a user can decline.
	 *
	 * Does nothing when the import carried no skin, which is what a static mesh is.
	 *
	 * @throws std::runtime_error if either file cannot be written.
	 */
	static void
	WriteImportedRig(
		const assetlib::imp::BMeshImport& imported,
		assetlib::BMesh&                  mesh,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      bskelPath,
		const std::filesystem::path&      banimPath,
		bool                              writeClips);

	/**
	 * Writes `mesh` to `bmeshPath`, creating the directory the path names first: an import aimed
	 * at a subfolder lands at `Meshes/<folder>/`, which nothing else creates -- `assetlib::save`
	 * opens the file where it stands.
	 *
	 * @throws std::runtime_error if the file cannot be written.
	 */
	static void
	WriteImportedMesh(const assetlib::BMesh& mesh, const std::filesystem::path& bmeshPath);

	/** A file an import writes, and whether the import is the one that made it. */
	struct ImportedFile
	{
		std::filesystem::path path;
		bool                  existed = false;  // whether it was there before the import started
	};

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
	 *
	 * A materials folder shared with another import is why the material files are listed in `files`
	 * one by one: the folder cannot be taken down, so the only way to leave the other import intact is
	 * to remove exactly the files this one wrote.
	 */
	static void
	RollBack(std::span<const ImportedFile> files, std::span<const ImportedDir> dirs);

	/**
	 * The `.bskel` under `dataRoot` whose signature matches `skeleton`, or empty when none does.
	 *
	 * What lets an import with the mesh turned off find the rig its clips belong to.
	 *
	 * @throws std::runtime_error if more than one rig matches, since which one the clips attach to
	 *         would otherwise depend on directory order.
	 */
	[[nodiscard]] static std::filesystem::path
	FindMatchingSkeleton(const std::filesystem::path& dataRoot, const assetlib::Skeleton& skeleton);

	/**
	 * Writes only `imported`'s clips, attached to a rig already in the project -- what an import with
	 * the mesh turned off does, and how a rig whose animations the artist exported one per file gets
	 * all of them without a copy of the geometry each time.
	 *
	 * @throws std::runtime_error if the file carries no clips or no rig, or if no skeleton in
	 *         `dataRoot` matches the one it was authored against.
	 */
	static void
	WriteImportedClips(
		const assetlib::imp::BMeshImport& imported,
		const std::filesystem::path&      dataRoot,
		const std::filesystem::path&      banimPath);

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

	/** What became of an import, so a multi-file drop knows whether to carry on with the next one. */
	enum class ImportOutcome
	{
		kImported,
		kCancelled,  // by the user, on the loading screen
		kBlocked,    // an asset of that name already exists; reported to the user
		kFailed,     // already reported to the user
	};

	/** What the import dialog asked for. */
	struct ImportOptions
	{
		// Every file to write, already inside its own category.
		ImportOutputs outputs;

		bool mesh         = true;  // off imports only the pieces below -- see ImportMesh
		bool textures     = false;
		bool pbrMaterials = false;  // ignored without textures -- a material routes at those
		bool animations   = false;  // the clips; the skeleton rides with the mesh
	};

	/**
	 * Converts a dropped glTF/glb into the engine's on-disk form.
	 *
	 * Which category each piece lands in is decided by what it is, not by where the drop happened: the
	 * mesh under `Meshes/`, the rig under `Skeletons/`, the clips under `Animations/`.
	 * `options.destinations` says where inside each -- a project's references are written against that
	 * layout, so an import may organise within a category and never across one.
	 *
	 * Parsing and supercompressing the textures run on a worker thread behind a cancellable loading
	 * screen: they take long enough to freeze the editor. Nothing there touches bgl. The material
	 * graphs are built afterwards, back on the UI thread -- their nodes own QPixmaps, which belong to
	 * it -- so the `.bmesh` is written from the UI thread too, once its materials exist to be named.
	 *
	 * Refuses to overwrite anything, reports a failure to the user, and on either a failure or a cancel
	 * removes the half-written files it had produced -- see RollBack.
	 *
	 * What counts as a collision differs by category. A materials folder may be shared with another
	 * import, since `options.outputs` names each file, so only a colliding *file* refuses this one. A
	 * texture folder may not: `writeTextures` names its output by index, so one already there is
	 * another import's and writing into it would overwrite that import's files.
	 */
	[[nodiscard]] ImportOutcome
	ImportMesh(const QString& sourceFile, const ImportOptions& options);

	/**
	 * Converts a dropped Radiance `.hdr` into the environment family: a `.bsky`, the `.benvl`
	 * convolved from the same radiance, and the `.benv` naming the pair.
	 *
	 * Unlike ImportMesh there is no RollBack here -- `assetlib::importEnvironment` undoes its own
	 * half-written work, including on a cancel, so the editor has nothing to clean up after.
	 */
	[[nodiscard]] ImportOutcome
	ImportEnvironment(const QString& sourceFile);

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

	/**
	 * Whether the Material Editor holds `absolute` -- or, when it is a directory, anything beneath it
	 * -- open. What every on-disk operation here is gated on, because an open graph's next Save would
	 * write the old state straight back.
	 */
	[[nodiscard]] bool
	IsHeldOpen(const QString& absolute, bool isDirectory) const;

	/**
	 * Deletes `asset` (data-root-relative), having first established that nothing references it: no
	 * material samples the texture, no mesh names the material.
	 *
	 * Deleting a mesh is never refused -- the materials it named are shareable assets. They stay
	 * where they are, and the maps a deleted material leaves behind are what Clean Unused Textures
	 * sweeps.
	 */
	void
	DeleteAsset(const QString& asset);

	/**
	 * DeleteAsset, taking also every asset that nothing would reference once the target is gone --
	 * listed in the confirmation first, and blocked by the Material Editor holding any of it open.
	 */
	void
	DeleteAssetCascade(const QString& asset);

	/** The body Delete and Delete Cascade share; `planner` is the one thing they differ by. */
	void
	DeleteWithPlanner(
		const QString& asset,
		assetlib::DeletionPlan (*planner)(const assetlib::AssetRefGraph&, std::string_view));

	/**
	 * Renames `asset` (data-root-relative) in place, rewriting every asset that references it so the
	 * rename never breaks an edge -- which is why it is not blocked by references the way Delete is.
	 * The dialog edits a file's stem alone: the extension says what the asset is, and renaming must
	 * not change that.
	 */
	void
	RenameAsset(const QString& asset);

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
	QStringList               m_History;
	AssetsHeldOpenFn          m_AssetsHeldOpen;

	// A pure-CPU decode with no renderer behind it, so the explorer stands its own cache instead of
	// being handed one the way the GPU-backed AssetThumbnailCache is.
	TexturePreviewCache m_TexturePreviews;
};
