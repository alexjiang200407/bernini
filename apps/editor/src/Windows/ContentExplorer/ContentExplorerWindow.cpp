#include "ContentExplorerWindow.h"

#include "Async/BackgroundTask.h"
#include "Project/Project.h"
#include "Windows/AssetImporter/AssetImporterDialog.h"

#include <QAbstractItemView>
#include <QAction>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QStringList>
#include <QUrl>

#include <assetlib/asset_refs.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/material_bake.h>

namespace
{
	// Tile geometry: the thumbnail box, and the cell that holds it plus a name beneath.
	constexpr int c_TileIconDim = 128;
	constexpr int c_TileWidth   = 168;
	constexpr int c_TileHeight  = 190;

	// Mesh file extensions the importer accepts.
	bool
	IsImportableMesh(const QString& localFile)
	{
		const auto ext = QFileInfo(localFile).suffix().toLower();
		return ext == "glb" || ext == "gltf";
	}

	/**
	 * Asks before an import writes over anything that is already there, listing what it would replace.
	 *
	 * Both an existing `.bmesh` and an existing texture folder are destructive to import onto: the mesh
	 * is simply overwritten, and the extracted textures are named tex0.ktx2, tex1.ktx2 ... by index, so
	 * they land on top of whatever the folder's previous occupant left under those names. Neither is
	 * recoverable, and neither is something the OS will refuse on the user's behalf.
	 *
	 * @return true to go ahead, false if the user would rather not.
	 */
	bool
	ConfirmOverwrite(
		QWidget*                     parent,
		const QString&               name,
		const std::filesystem::path& bmeshPath,
		bool                         bmeshExists,
		const std::filesystem::path& textureDir,
		bool                         textureDirExists)
	{
		auto replaced = QStringList();

		if (bmeshExists)
			replaced << QString::fromStdWString(bmeshPath.wstring());
		if (textureDirExists)
			replaced << QString::fromStdWString(textureDir.wstring());

		if (replaced.isEmpty())
			return true;

		auto confirm = QMessageBox(parent);
		confirm.setWindowTitle("Import Asset");
		confirm.setIcon(QMessageBox::Warning);
		confirm.setText(QString("Importing '%1' will overwrite existing files.").arg(name));
		confirm.setInformativeText(
			"The mesh is replaced, and extracted textures are written as tex0.ktx2, tex1.ktx2\n"
			"and so on by index -- landing on top of any file already using those names.\n\n"
			"This cannot be undone.");
		confirm.setDetailedText(replaced.join('\n'));

		auto* overwrite = confirm.addButton("Overwrite", QMessageBox::DestructiveRole);
		confirm.addButton(QMessageBox::Cancel);
		confirm.setDefaultButton(QMessageBox::Cancel);
		confirm.exec();

		return confirm.clickedButton() == overwrite;
	}
}

ContentExplorerWindow::ContentExplorerWindow(QWidget* parent, AssetsHeldOpenFn assetsHeldOpen) :
	QWidget(parent), m_AssetsHeldOpen(std::move(assetsHeldOpen))
{
	m_Ui.setupUi(this);

	m_Ui.splitter->setStretchFactor(0, 0);
	m_Ui.splitter->setStretchFactor(1, 1);
	m_Ui.splitter->setSizes({ 220, 700 });

	// The hierarchy shows files as well as directories, so an asset can be found and dragged straight
	// out of the tree without first navigating to its folder in the right-hand view.
	m_HierarchyModel = new QFileSystemModel(this);
	m_HierarchyModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

	m_FileModel = new AssetFileModel(this);
	m_FileModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

	m_Ui.FileExplorer->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(
		m_Ui.FileExplorer,
		&QWidget::customContextMenuRequested,
		this,
		&ContentExplorerWindow::ShowHierarchyMenu);

	m_Ui.CurrentDirectoryExplorer->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(
		m_Ui.CurrentDirectoryExplorer,
		&QWidget::customContextMenuRequested,
		this,
		&ContentExplorerWindow::ShowFileMenu);

	auto* viewport = m_Ui.CurrentDirectoryExplorer->viewport();
	m_EmptyPlaceholder =
		new QLabel("Nothing exists in this directory.\nRight-click to add.", viewport);
	m_EmptyPlaceholder->setAlignment(Qt::AlignCenter);
	m_EmptyPlaceholder->setWordWrap(true);
	m_EmptyPlaceholder->setAttribute(Qt::WA_TransparentForMouseEvents);
	m_EmptyPlaceholder->setStyleSheet("color: gray;");
	m_EmptyPlaceholder->hide();
	viewport->installEventFilter(this);

	// The item views don't accept drops, so dropped mesh files bubble up to this widget.
	setAcceptDrops(true);

	// No project is open yet: the explorer stays disabled and empty until rooted.
	Clear();
}

void
ContentExplorerWindow::SetThumbnails(AssetThumbnailCache* thumbnails)
{
	m_FileModel->SetThumbnails(thumbnails);
}

void
ContentExplorerWindow::SetRootPath(const QString& path)
{
	AttachModels();
	setEnabled(true);
	m_RootPath = path;

	m_Ui.FileExplorer->setRootIndex(m_HierarchyModel->setRootPath(path));
	m_Ui.CurrentDirectoryExplorer->setRootIndex(m_FileModel->setRootPath(path));
	UpdateEmptyPlaceholder();
}

void
ContentExplorerWindow::AttachModels()
{
	if (m_Ui.FileExplorer->model() == m_HierarchyModel)
		return;

	m_Ui.FileExplorer->setModel(m_HierarchyModel);
	m_Ui.FileExplorer->setHeaderHidden(true);
	for (auto column = 1; column < m_HierarchyModel->columnCount(); ++column)
		m_Ui.FileExplorer->hideColumn(column);

	m_Ui.CurrentDirectoryExplorer->setModel(m_FileModel);
	m_Ui.CurrentDirectoryExplorer->setEditTriggers(QAbstractItemView::NoEditTriggers);

	// A grid of tiles, each an asset's thumbnail above its name.
	m_Ui.CurrentDirectoryExplorer->setViewMode(QListView::IconMode);
	m_Ui.CurrentDirectoryExplorer->setIconSize(QSize(c_TileIconDim, c_TileIconDim));
	m_Ui.CurrentDirectoryExplorer->setGridSize(QSize(c_TileWidth, c_TileHeight));
	m_Ui.CurrentDirectoryExplorer->setResizeMode(QListView::Adjust);
	m_Ui.CurrentDirectoryExplorer->setUniformItemSizes(true);
	m_Ui.CurrentDirectoryExplorer->setWordWrap(true);

	// IconMode lets the user shuffle tiles around the grid by default, which would imply an ordering
	// the folder does not have.
	m_Ui.CurrentDirectoryExplorer->setMovement(QListView::Static);

	// Assets can be dragged out of the explorer (e.g. a .bmesh onto the Material Editor preview).
	// QFileSystemModel supplies the file URLs; DragOnly keeps the views from accepting drops, so
	// dropped mesh files still bubble up to this widget's dropEvent for import.
	m_Ui.FileExplorer->setDragEnabled(true);
	m_Ui.FileExplorer->setDragDropMode(QAbstractItemView::DragOnly);
	m_Ui.CurrentDirectoryExplorer->setDragEnabled(true);
	m_Ui.CurrentDirectoryExplorer->setDragDropMode(QAbstractItemView::DragOnly);

	// Selecting an entry on the left shows the containing folder's contents on the right. The tree
	// lists files too, and a file is not a directory to root the right-hand view at, so selecting one
	// shows the folder it lives in.
	connect(
		m_Ui.FileExplorer->selectionModel(),
		&QItemSelectionModel::currentChanged,
		this,
		[this](const QModelIndex& current, const QModelIndex&) {
			if (!current.isValid())
				return;

			const QModelIndex folder =
				m_HierarchyModel->isDir(current) ? current : current.parent();
			if (!folder.isValid())
				return;

			const auto path = m_HierarchyModel->filePath(folder);
			m_Ui.CurrentDirectoryExplorer->setRootIndex(m_FileModel->setRootPath(path));
			UpdateEmptyPlaceholder();
		});

	// Double-clicking a folder on the right opens it.
	connect(
		m_Ui.CurrentDirectoryExplorer,
		&QAbstractItemView::doubleClicked,
		this,
		[this](const QModelIndex& index) {
			if (!m_FileModel->isDir(index))
				return;
			const auto path = m_FileModel->filePath(index);
			m_Ui.CurrentDirectoryExplorer->setRootIndex(m_FileModel->setRootPath(path));
			UpdateEmptyPlaceholder();
		});

	// The model populates directories asynchronously and mutates as folders are added or
	// removed, so re-evaluate the placeholder whenever the shown folder's contents change.
	connect(m_FileModel, &QFileSystemModel::directoryLoaded, this, [this](const QString&) {
		UpdateEmptyPlaceholder();
	});
	connect(m_FileModel, &QAbstractItemModel::rowsInserted, this, [this]() {
		UpdateEmptyPlaceholder();
	});
	connect(m_FileModel, &QAbstractItemModel::rowsRemoved, this, [this]() {
		UpdateEmptyPlaceholder();
	});
}

void
ContentExplorerWindow::Clear()
{
	m_Ui.FileExplorer->setModel(nullptr);
	m_Ui.CurrentDirectoryExplorer->setModel(nullptr);
	m_EmptyPlaceholder->hide();
	setEnabled(false);
}

void
ContentExplorerWindow::UpdateEmptyPlaceholder()
{
	auto* viewport = m_Ui.CurrentDirectoryExplorer->viewport();

	// Only meaningful once a folder is shown; and while the model is still fetching the
	// directory's contents we can't yet tell whether it's empty, so wait for the reload.
	const auto root  = m_Ui.CurrentDirectoryExplorer->rootIndex();
	const bool empty = m_Ui.CurrentDirectoryExplorer->model() == m_FileModel &&
	                   !m_FileModel->canFetchMore(root) && m_FileModel->rowCount(root) == 0;

	if (empty)
	{
		m_EmptyPlaceholder->setGeometry(viewport->rect());
		m_EmptyPlaceholder->show();
	}
	else
	{
		m_EmptyPlaceholder->hide();
	}
}

bool
ContentExplorerWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_Ui.CurrentDirectoryExplorer->viewport() && event->type() == QEvent::Resize)
	{
		m_EmptyPlaceholder->setGeometry(m_Ui.CurrentDirectoryExplorer->viewport()->rect());
	}

	return QWidget::eventFilter(watched, event);
}

void
ContentExplorerWindow::ShowHierarchyMenu(const QPoint& pos)
{
	if (m_Ui.FileExplorer->model() != m_HierarchyModel)
		return;

	ShowAssetMenu(*m_Ui.FileExplorer, *m_HierarchyModel, pos);
}

void
ContentExplorerWindow::ShowFileMenu(const QPoint& pos)
{
	if (m_Ui.CurrentDirectoryExplorer->model() != m_FileModel)
		return;

	ShowAssetMenu(*m_Ui.CurrentDirectoryExplorer, *m_FileModel, pos);
}

void
ContentExplorerWindow::ShowAssetMenu(
	QAbstractItemView& view,
	QFileSystemModel&  model,
	const QPoint&      pos)
{
	const QModelIndex index = view.indexAt(pos);

	QModelIndex parent = view.rootIndex();
	if (index.isValid())
		parent = model.isDir(index) ? index : index.parent();
	if (!parent.isValid())
		parent = view.rootIndex();

	const QString parentPath = model.filePath(parent);
	const QString asset      = AssetAt(model, index, m_RootPath);

	auto  menu   = QMenu(this);
	auto* addDir = menu.addAction("Add Directory");

	QAction* bake   = nullptr;
	QAction* remove = nullptr;
	if (!asset.isEmpty())
	{
		menu.addSeparator();
		if (IsMaterialAsset(asset))
			bake = menu.addAction("Bake");
		remove = menu.addAction("Delete");
	}

	QAction* const chosen = menu.exec(view.viewport()->mapToGlobal(pos));

	if (chosen == addDir)
		AddDirectory(&model, parentPath);
	else if (bake != nullptr && chosen == bake)
		BakeMaterial(asset);
	else if (remove != nullptr && chosen == remove)
		DeleteAsset(asset);
}

bool
ContentExplorerWindow::IsMaterialAsset(const QString& asset)
{
	const std::optional<assetlib::AssetType> type =
		assetlib::assetTypeFromExtension(asset.toStdWString());
	return type && *type == assetlib::AssetType::kMaterial;
}

void
ContentExplorerWindow::BakeMaterial(const QString& asset)
{
	const std::filesystem::path dataRoot = m_RootPath.toStdWString();
	const std::filesystem::path materialPath =
		dataRoot / std::filesystem::path(asset.toStdWString());

	auto desc     = assetlib::MaterialBakeDesc();
	desc.dataRoot = dataRoot;

	auto material = assetlib::BMaterial();

	// Compositing decodes, resizes and re-encodes a KTX2 for each map, so it runs off the UI thread. It
	// touches files only, never bgl. Baking reads the material off disk, so the routes it composites are
	// the ones last saved -- Save in the Material Editor first to bake unsaved edits.
	const background::TaskResult result = background::RunWithLoadingScreen(
		this,
		QString("Baking %1").arg(QFileInfo(asset).fileName()),
		[&](background::Progress& progress) {
			progress.Report(0, 0, "Reading material...");
			material = assetlib::loadMaterial(materialPath);

			progress.Report(0, 0, "Compositing maps...");
			assetlib::bakeMaterial(material, desc, progress.Cancellation());

			progress.Report(0, 0, "Writing material...");
			assetlib::saveMaterial(material, materialPath);
		},
		background::Cancellable::kYes);

	// A map bakeMaterial wrote is named by the hash of its inputs, so a cancelled or re-run bake leaves
	// only correct, reusable files.
	if (result.Cancelled())
		return;

	if (result.Failed())
	{
		QMessageBox::warning(
			this,
			"Bake Material",
			QString("Could not bake '%1':\n\n%2").arg(QFileInfo(asset).fileName(), result.error));
		return;
	}

	// The thumbnail cache watches the material's mtime and repaints itself; the Material Editor does
	// not, and is showing what this file said before the bake.
	Q_EMIT MaterialBaked(asset);
}

QString
ContentExplorerWindow::AssetAt(
	const QFileSystemModel& model,
	const QModelIndex&      index,
	const QString&          dataRoot)
{
	if (!index.isValid() || dataRoot.isEmpty())
		return {};

	const QString path     = model.filePath(index);
	const QString relative = QDir(dataRoot).relativeFilePath(path);

	// Something outside the project is not the project's to delete, whatever it is named.
	if (relative.isEmpty() || relative == "." || relative.startsWith(".."))
		return {};

	if (model.isDir(index))
	{
		return Project::IsRequiredDirectory(relative.toStdWString()) ? QString() : relative;
	}

	return assetlib::assetTypeFromExtension(path.toStdWString()) ? relative : QString();
}

void
ContentExplorerWindow::DeleteAsset(const QString& asset)
{
	const QString absolute    = QDir(m_RootPath).absoluteFilePath(asset);
	const bool    isDirectory = QFileInfo(absolute).isDir();

	const auto holdsOpen = [&](const QString& open) {
		if (!isDirectory)
			return QFileInfo(open) == QFileInfo(absolute);

		return !QDir(absolute).relativeFilePath(open).startsWith("..");
	};

	for (const QString& open : m_AssetsHeldOpen())
	{
		if (!holdsOpen(open))
			continue;

		QMessageBox::warning(
			this,
			"Delete",
			QString(
				"%1 is open in the Material Editor.\n\nClose it there first, or saving it "
				"would write it back.")
				.arg(
					isDirectory ? QString("'%1' holds a material that").arg(asset) :
								  QString("'%1'").arg(asset)));
		return;
	}

	auto desc     = assetlib::AssetRefScanDesc();
	desc.dataRoot = m_RootPath.toStdWString();

	auto graph = std::optional<assetlib::AssetRefGraph>();

	// The scan parses every mesh and material in the project, so it runs off the UI thread. It reads
	// assetlib only, never bgl, which is what the loading screen requires of its worker. It takes no
	// cancel token, so the screen offers no button that would not work.
	const background::TaskResult scanned =
		background::RunWithLoadingScreen(this, "Delete", [&](background::Progress& progress) {
			progress.Report(0, 0, "Checking references...");
			graph = assetlib::AssetRefGraph::Scan(desc);
		});

	if (!scanned.Completed())
	{
		// A mesh or material that will not parse aborts the scan, and rightly so: its references cannot
		// be known, and one of them may be the file about to be deleted.
		QMessageBox::warning(
			this,
			"Delete",
			QString("Could not work out what references '%1', so it was not deleted:\n\n%2")
				.arg(asset, scanned.error));
		return;
	}

	const assetlib::DeletionPlan plan = assetlib::planDeletion(*graph, asset.toStdString());

	if (!plan.Allowed())
	{
		auto referrers = QStringList();
		for (const assetlib::AssetRef& ref : plan.blockers)
			referrers << QString::fromStdString(ref.referrer);
		referrers.removeDuplicates();
		referrers.sort();

		const bool one = referrers.size() == 1;

		auto blocked = QMessageBox(this);
		blocked.setWindowTitle("Delete");
		blocked.setIcon(QMessageBox::Warning);
		blocked.setText(QString("'%1' cannot be deleted.").arg(asset));
		blocked.setInformativeText(
			isDirectory ?
				QString(
					"%1 outside this folder still %2 something inside it. Re-route or delete "
					"%3 first.")
					.arg(one ? QString("One asset") : QString("%1 assets").arg(referrers.size()))
					.arg(one ? "references" : "reference")
					.arg(one ? "it" : "them") :
				QString("%1 still %2 it. Re-route or delete %3 first.")
					.arg(one ? QString("One asset") : QString("%1 assets").arg(referrers.size()))
					.arg(one ? "references" : "reference")
					.arg(one ? "it" : "them"));
		blocked.setDetailedText(referrers.join('\n'));
		blocked.exec();
		return;
	}

	auto contents = QStringList();
	for (const std::string& file : plan.contents) contents << QString::fromStdString(file);

	auto confirm = QMessageBox(this);
	confirm.setWindowTitle("Delete");
	confirm.setIcon(QMessageBox::Warning);

	if (plan.IsDirectory())
	{
		confirm.setText(QString("Delete '%1' and everything in it?").arg(asset));
		confirm.setInformativeText(
			contents.isEmpty() ?
				QString("The folder is empty.\n\nThis cannot be undone.") :
				QString(
					"%1 file(s) will be deleted. Nothing outside the folder references any of "
					"them.\n\nThis cannot be undone.")
					.arg(contents.size()));
		confirm.setDetailedText(contents.join('\n'));
	}
	else if (plan.assetType == assetlib::AssetType::kMesh)
	{
		// The one kind whose deletion leaves something behind, and the user should not have to wonder
		// whether it took the materials with it.
		confirm.setText(QString("Delete '%1'?").arg(asset));
		confirm.setInformativeText(
			"Nothing references it. The materials it uses are shared, and stay in place.");
	}
	else
	{
		confirm.setText(QString("Delete '%1'?").arg(asset));
		confirm.setInformativeText("Nothing references it. This cannot be undone.");
	}

	auto* remove = confirm.addButton("Delete", QMessageBox::DestructiveRole);
	confirm.addButton(QMessageBox::Cancel);
	confirm.setDefaultButton(QMessageBox::Cancel);
	confirm.exec();

	if (confirm.clickedButton() != remove)
		return;

	const assetlib::DeletionResult result = assetlib::deleteAsset(plan, desc);

	switch (result.status)
	{
	case assetlib::DeletionStatus::kDeleted:
		// The model watches the directory, so the row goes on its own -- but a view rooted *inside* what
		// just went is left showing a folder that no longer exists, and has nowhere to navigate back to.
		if (isDirectory)
		{
			const QString shown = m_FileModel->filePath(m_Ui.CurrentDirectoryExplorer->rootIndex());

			if (!QDir(absolute).relativeFilePath(shown).startsWith(".."))
				m_Ui.CurrentDirectoryExplorer->setRootIndex(m_FileModel->setRootPath(m_RootPath));
		}
		return;

	case assetlib::DeletionStatus::kFailed:
		QMessageBox::warning(
			this,
			"Delete",
			QString("'%1' could not be deleted:\n\n%2\n\nIt may be open in another program.")
				.arg(asset, QString::fromStdString(result.error)));
		return;

	case assetlib::DeletionStatus::kRefused:
		// Something wrote a reference to it between the scan and the confirmation.
		QMessageBox::warning(
			this,
			"Delete",
			QString("'%1' is referenced again, and was not deleted.").arg(asset));
		return;
	}
}

void
ContentExplorerWindow::AddDirectory(QFileSystemModel* model, const QString& parentPath)
{
	if (parentPath.isEmpty())
		return;

	bool       ok   = false;
	const auto name = QInputDialog::getText(
						  this,
						  "Add Directory",
						  "Directory name:",
						  QLineEdit::Normal,
						  "New Folder",
						  &ok)
	                      .trimmed();
	if (!ok || name.isEmpty())
		return;

	const QModelIndex parent = model->index(parentPath);

	if (!parent.isValid() || !model->mkdir(parent, name).isValid())
		QMessageBox::warning(
			this,
			"Add Directory",
			QString("Could not create directory '%1'.").arg(name));
}

void
ContentExplorerWindow::dragEnterEvent(QDragEnterEvent* event)
{
	const auto* mime = event->mimeData();
	if (!mime->hasUrls())
		return;

	for (const QUrl& url : mime->urls())
	{
		if (url.isLocalFile() && IsImportableMesh(url.toLocalFile()))
		{
			event->acceptProposedAction();
			return;
		}
	}
}

void
ContentExplorerWindow::dragMoveEvent(QDragMoveEvent* event)
{
	// The accept decision doesn't depend on position, so mirror dragEnterEvent.
	event->acceptProposedAction();
}

void
ContentExplorerWindow::dropEvent(QDropEvent* event)
{
	const QString targetDir = ResolveDropDirectory(event->position().toPoint());
	if (targetDir.isEmpty())
		return;

	for (const QUrl& url : event->mimeData()->urls())
	{
		if (!url.isLocalFile())
			continue;

		const QString file = url.toLocalFile();
		if (!IsImportableMesh(file))
			continue;

		AssetImporterDialog dialog(file, targetDir, this);
		if (dialog.exec() != QDialog::Accepted)
			continue;

		const ImportOutcome outcome = ImportMesh(
			file,
			targetDir,
			dialog.ImportTextures() ? dialog.TextureSubdirectory() : QString());

		// Cancelling one import of a multi-file drop abandons the drop. Carrying on would answer the
		// user's "stop" by immediately putting the next options dialog in front of them.
		if (outcome == ImportOutcome::kCancelled)
			break;
	}

	event->acceptProposedAction();
}

QString
ContentExplorerWindow::ResolveDropDirectory(const QPoint& windowPos) const
{
	const QPoint global = mapToGlobal(windowPos);

	// Dropped over a folder row in the file table -> that folder; otherwise the folder the
	// table currently shows.
	auto*        fileViewport = m_Ui.CurrentDirectoryExplorer->viewport();
	const QPoint fileLocal    = fileViewport->mapFromGlobal(global);
	if (fileViewport->rect().contains(fileLocal))
	{
		const auto index = m_Ui.CurrentDirectoryExplorer->indexAt(fileLocal);
		if (index.isValid() && m_FileModel->isDir(index))
			return m_FileModel->filePath(index);
		return m_FileModel->rootPath();
	}

	// Dropped over a folder in the hierarchy tree.
	auto*        treeViewport = m_Ui.FileExplorer->viewport();
	const QPoint treeLocal    = treeViewport->mapFromGlobal(global);
	if (treeViewport->rect().contains(treeLocal))
	{
		const auto index = m_Ui.FileExplorer->indexAt(treeLocal);
		if (index.isValid())
			return m_HierarchyModel->filePath(index);
	}

	return m_FileModel->rootPath();
}

ContentExplorerWindow::ImportOutcome
ContentExplorerWindow::ImportMesh(
	const QString& sourceFile,
	const QString& targetDir,
	const QString& textureSubdir)
{
	namespace fs = std::filesystem;

	const fs::path source  = fs::path(sourceFile.toStdWString());
	const fs::path meshDir = fs::path(targetDir.toStdWString());

	// writeTextures names its output tex0.ktx2, tex1.ktx2 ... by index, so every import needs its
	// own folder or the next one silently overwrites it. m_RootPath is the project's Data directory.
	//
	// Left empty when no textures are being extracted, and it must stay that way: joining an empty
	// subdirectory would name the texture root itself, which RollBack would then happily delete.
	const bool     importTextures = !textureSubdir.isEmpty();
	const fs::path textureDir     = importTextures ? fs::path(m_RootPath.toStdWString()) /
	                                                     AssetImporterDialog::c_TextureRoot /
	                                                     fs::path(textureSubdir.toStdWString()) :
	                                                 fs::path();

	fs::path bmeshPath = meshDir / source.filename();
	bmeshPath.replace_extension(".bmesh");

	const QString name = QFileInfo(sourceFile).fileName();

	// Sampled before a byte is written, because they decide two things: whether the user has to be
	// asked first, and -- if the import then fails or is cancelled -- what may be deleted to undo it.
	std::error_code ec;
	const bool      bmeshExisted      = fs::exists(bmeshPath, ec);
	const bool      textureDirExisted = !textureDir.empty() && fs::exists(textureDir, ec);

	if (!ConfirmOverwrite(this, name, bmeshPath, bmeshExisted, textureDir, textureDirExisted))
		return ImportOutcome::kCancelled;

	// Parsing the glTF and, above all, Basis-supercompressing its textures take long enough to
	// freeze the editor for minutes on a large asset. None of it touches bgl, so the whole import
	// runs on a worker; only the message box below is back on the UI thread.
	const background::TaskResult result = background::RunWithLoadingScreen(
		this,
		QString("Importing %1").arg(name),
		[&](background::Progress& progress) {
			const assetlib::CancelToken cancel = progress.Cancellation();

			progress.Report(0, 0, QString("Parsing %1...").arg(name));
			const auto mesh = assetlib::loadFromGltf(source, cancel);

			progress.Report(0, 0, "Writing mesh...");
			assetlib::save(assetlib::toBMesh(mesh), bmeshPath);

			if (importTextures)
			{
				assetlib::writeTextures(
					mesh,
					textureDir,
					[&](size_t done, size_t total) {
						progress.Report(
							static_cast<int>(done),
							static_cast<int>(total),
							QString("Compressing textures (%1 of %2)...").arg(done + 1).arg(total));
					},
					cancel);
			}
		},
		background::Cancellable::kYes);

	if (result.Completed())
		return ImportOutcome::kImported;

	// A cancelled cook throws where it stood, so the mesh may be on disk pointing at textures that were
	// never extracted. Neither outcome may leave that behind for the user to trip over later.
	RollBack(bmeshPath, bmeshExisted, textureDir, textureDirExisted);

	if (result.Cancelled())
		return ImportOutcome::kCancelled;

	QMessageBox::warning(
		this,
		"Import Asset",
		QString("Failed to import '%1':\n\n%2").arg(name, result.error));

	return ImportOutcome::kFailed;
}

void
ContentExplorerWindow::RollBack(
	const std::filesystem::path& bmeshPath,
	bool                         bmeshExisted,
	const std::filesystem::path& textureDir,
	bool                         textureDirExisted)
{
	namespace fs = std::filesystem;

	std::error_code ec;

	if (!bmeshExisted)
		fs::remove(bmeshPath, ec);

	// remove_all is recursive, so the folder it is handed had better be the one this import made. An
	// empty path means no textures were extracted; a path naming the texture root itself would mean the
	// import's subdirectory got lost somewhere, and taking the root down with it is not a recovery.
	const auto textureRoot = fs::path(AssetImporterDialog::c_TextureRoot);
	if (textureDirExisted || textureDir.empty() || textureDir.filename() == textureRoot)
		return;

	fs::remove_all(textureDir, ec);
}
