#include "ContentExplorerWindow.h"

#include "Async/BackgroundTask.h"
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
#include <QStringList>
#include <QUrl>

#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>

namespace
{
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

ContentExplorerWindow::ContentExplorerWindow(QWidget* parent) : QWidget(parent)
{
	m_Ui.setupUi(this);

	// The hierarchy shows files as well as directories, so an asset can be found and dragged straight
	// out of the tree without first navigating to its folder in the right-hand view.
	m_HierarchyModel = new QFileSystemModel(this);
	m_HierarchyModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

	m_FileModel = new QFileSystemModel(this);
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
	m_Ui.CurrentDirectoryExplorer->verticalHeader()->setVisible(false);

	// Assets can be dragged out of the explorer (e.g. a .bmesh onto the Material Editor preview).
	// QFileSystemModel supplies the file URLs; DragOnly keeps the views from accepting drops, so
	// dropped mesh files still bubble up to this widget's dropEvent for import.
	m_Ui.FileExplorer->setDragEnabled(true);
	m_Ui.FileExplorer->setDragDropMode(QAbstractItemView::DragOnly);
	m_Ui.CurrentDirectoryExplorer->setDragEnabled(true);
	m_Ui.CurrentDirectoryExplorer->setDragDropMode(QAbstractItemView::DragOnly);

	// Show only Name and Last Modified, with Name taking the remaining width.
	m_Ui.CurrentDirectoryExplorer->hideColumn(1);  // Size
	m_Ui.CurrentDirectoryExplorer->hideColumn(2);  // Type
	auto* fileHeader = m_Ui.CurrentDirectoryExplorer->horizontalHeader();
	fileHeader->setStretchLastSection(false);
	fileHeader->setSectionResizeMode(0, QHeaderView::Stretch);           // Name
	fileHeader->setSectionResizeMode(3, QHeaderView::ResizeToContents);  // Last Modified

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

	// Add the new directory inside the clicked folder, or at the tree root when the click missed a
	// row. The tree lists files too, so a click on one targets the folder that contains it.
	const auto index = m_Ui.FileExplorer->indexAt(pos);

	QModelIndex parent = m_Ui.FileExplorer->rootIndex();
	if (index.isValid())
		parent = m_HierarchyModel->isDir(index) ? index : index.parent();
	if (!parent.isValid())
		parent = m_Ui.FileExplorer->rootIndex();

	auto  menu   = QMenu(this);
	auto* addDir = menu.addAction("Add Directory");
	if (menu.exec(m_Ui.FileExplorer->viewport()->mapToGlobal(pos)) == addDir)
		AddDirectory(m_HierarchyModel, parent);
}

void
ContentExplorerWindow::ShowFileMenu(const QPoint& pos)
{
	if (m_Ui.CurrentDirectoryExplorer->model() != m_FileModel)
		return;

	const auto index  = m_Ui.CurrentDirectoryExplorer->indexAt(pos);
	const auto parent = (index.isValid() && m_FileModel->isDir(index)) ?
	                        index :
	                        m_Ui.CurrentDirectoryExplorer->rootIndex();

	auto  menu   = QMenu(this);
	auto* addDir = menu.addAction("Add Directory");
	if (menu.exec(m_Ui.CurrentDirectoryExplorer->viewport()->mapToGlobal(pos)) == addDir)
		AddDirectory(m_FileModel, parent);
}

void
ContentExplorerWindow::AddDirectory(QFileSystemModel* model, const QModelIndex& parent)
{
	if (!parent.isValid())
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

	if (!model->mkdir(parent, name).isValid())
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
