#include "ContentExplorerWindow.h"

#include "Import/drop_import.h"
#include "Windows/ContentExplorer/AssetOperations.h"
#include "Windows/ContentExplorer/asset_rules.h"
#include "util/asset_paths.h"

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
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>

#include <tracy/Tracy.hpp>

namespace
{
	/** The folder `index` stands for: itself when it is one, otherwise the folder holding it. */
	QModelIndex
	FolderOf(const QFileSystemModel& model, const QModelIndex& index)
	{
		if (!index.isValid())
			return {};

		return model.isDir(index) ? index : index.parent();
	}

	// Tile geometry: the thumbnail box, and the cell that holds it plus a name beneath.
	constexpr int c_TileIconDim = 128;
	constexpr int c_TileWidth   = 168;
	constexpr int c_TileHeight  = 190;
}

ContentExplorerWindow::ContentExplorerWindow(QWidget* parent, AssetsHeldOpenFn assetsHeldOpen) :
	QWidget(parent)
{
	m_Ui.setupUi(this);

	m_Ui.splitter->setStretchFactor(0, 0);
	m_Ui.splitter->setStretchFactor(1, 1);
	m_Ui.splitter->setSizes({ 220, 700 });

	m_Ui.BackButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
	m_Ui.BackButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
	connect(m_Ui.BackButton, &QToolButton::clicked, this, &ContentExplorerWindow::NavigateBack);

	// The hierarchy shows files as well as directories, so an asset can be found and dragged straight
	// out of the tree without first navigating to its folder in the right-hand view.
	m_HierarchyModel = new QFileSystemModel(this);
	m_HierarchyModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

	m_FileModel = new AssetFileModel(this);
	m_FileModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
	m_FileModel->SetTexturePreviews(&m_TexturePreviews);

	connect(
		m_HierarchyModel,
		&QAbstractItemModel::rowsInserted,
		this,
		[this](const QModelIndex& parent, int first, int last) {
			HideBuildProductRows(m_Ui.FileExplorer, *m_HierarchyModel, parent, first, last);
		});
	connect(
		m_FileModel,
		&QAbstractItemModel::rowsInserted,
		this,
		[this](const QModelIndex& parent, int first, int last) {
			HideBuildProductRows(m_Ui.CurrentDirectoryExplorer, *m_FileModel, parent, first, last);
		});

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

	m_Operations = new AssetOperations(this, std::move(assetsHeldOpen));
	connect(
		m_Operations,
		&AssetOperations::MaterialBaked,
		this,
		&ContentExplorerWindow::MaterialBaked);
	connect(
		m_Operations,
		&AssetOperations::DirectoryDeleted,
		this,
		&ContentExplorerWindow::OnDirectoryDeleted);
	connect(
		m_Operations,
		&AssetOperations::DirectoryRenamed,
		this,
		&ContentExplorerWindow::OnDirectoryRenamed);

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
	ZoneScopedN("editor root content explorer");

	AttachModels();
	setEnabled(true);
	m_RootPath = path;
	m_Operations->SetDataRoot(path);
	m_FileModel->SetDataRoot(path);
	m_History.clear();

	m_Ui.FileExplorer->setRootIndex(m_HierarchyModel->setRootPath(path));
	ShowDirectory(path);
}

void
ContentExplorerWindow::ShowDirectory(const QString& path)
{
	m_Ui.CurrentDirectoryExplorer->setRootIndex(m_FileModel->setRootPath(path));
	HideBuildProductRows(
		m_Ui.CurrentDirectoryExplorer,
		*m_FileModel,
		m_Ui.CurrentDirectoryExplorer->rootIndex());

	// The tree follows, or it would go on highlighting the folder the grid has left -- and clicking
	// that row again would be a dead click, setCurrentIndex on the current index emitting nothing.
	// Not when it is already there: the tree is what navigates most of the time, and pulling its
	// current index onto the folder would take it off the file the user just clicked. Re-entering
	// through currentChanged is harmless -- the grid is rooted above, so NavigateTo returns early.
	if (QDir(m_HierarchyModel->filePath(
			FolderOf(*m_HierarchyModel, m_Ui.FileExplorer->currentIndex()))) != QDir(path))
	{
		// Cleared rather than set at the top of the tree, which is a root the tree has no row for.
		const QModelIndex folder = m_HierarchyModel->index(path);
		m_Ui.FileExplorer->setCurrentIndex(
			folder == m_Ui.FileExplorer->rootIndex() ? QModelIndex() : folder);
	}

	m_Ui.BackButton->setEnabled(!m_History.isEmpty());
	UpdateEmptyPlaceholder();
}

void
ContentExplorerWindow::NavigateTo(const QString& path)
{
	const QString shown = m_FileModel->filePath(m_Ui.CurrentDirectoryExplorer->rootIndex());

	if (!shown.isEmpty())
	{
		// Selecting a file in the tree navigates to the folder holding it, so the folder already
		// shown arrives here routinely; recording it would make Back a no-op that has to be pressed
		// twice.
		if (QDir(shown) == QDir(path))
			return;

		m_History.push_back(shown);
	}

	ShowDirectory(path);
}

void
ContentExplorerWindow::NavigateBack()
{
	while (!m_History.isEmpty())
	{
		const QString previous = m_History.takeLast();
		if (QDir(previous).exists())
		{
			ShowDirectory(previous);
			return;
		}
	}

	m_Ui.BackButton->setEnabled(false);
}

void
ContentExplorerWindow::HideBuildProductRows(
	QAbstractItemView*      view,
	const QFileSystemModel& model,
	const QModelIndex&      parent,
	const int               first,
	const int               last)
{
	auto* tree = qobject_cast<QTreeView*>(view);
	auto* list = qobject_cast<QListView*>(view);

	// A list's rows are its root's children; anything else that arrives is not on screen.
	if (list != nullptr && parent != list->rootIndex())
		return;

	const int end =
		last < 0 ? model.rowCount(parent) - 1 : std::min(last, model.rowCount(parent) - 1);
	for (int row = first; row <= end; ++row)
	{
		if (!editor::IsHiddenBuildProductFile(model.filePath(model.index(row, 0, parent))))
			continue;

		if (tree != nullptr)
			tree->setRowHidden(row, parent, true);
		else if (list != nullptr)
			list->setRowHidden(row, true);
	}
}

void
ContentExplorerWindow::AttachModels()
{
	if (m_Ui.FileExplorer->model() == m_HierarchyModel)
		return;

	m_Ui.FileExplorer->setModel(m_HierarchyModel);
	m_Ui.FileExplorer->setHeaderHidden(true);
	connect(m_Ui.FileExplorer, &QTreeView::expanded, this, [this](const QModelIndex& parent) {
		HideBuildProductRows(m_Ui.FileExplorer, *m_HierarchyModel, parent);
	});
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

			const QModelIndex folder = FolderOf(*m_HierarchyModel, current);
			if (!folder.isValid())
				return;

			NavigateTo(m_HierarchyModel->filePath(folder));
		});

	// Double-clicking a folder on the right opens it.
	connect(
		m_Ui.CurrentDirectoryExplorer,
		&QAbstractItemView::doubleClicked,
		this,
		[this](const QModelIndex& index) {
			if (!m_FileModel->isDir(index))
				return;

			NavigateTo(m_FileModel->filePath(index));
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
	m_History.clear();
	m_Ui.BackButton->setEnabled(false);
	setEnabled(false);
}

void
ContentExplorerWindow::UpdateEmptyPlaceholder()
{
	auto* viewport = m_Ui.CurrentDirectoryExplorer->viewport();

	// Only meaningful once a folder is shown; and while the model is still fetching the
	// directory's contents we can't yet tell whether it's empty, so wait for the reload.
	const auto root = m_Ui.CurrentDirectoryExplorer->rootIndex();

	// Counting what the view shows, not what the model holds: a folder of nothing but hidden
	// build products is empty to the user.
	auto visibleRows = 0;
	for (int row = 0; row < m_FileModel->rowCount(root); ++row)
		if (!m_Ui.CurrentDirectoryExplorer->isRowHidden(row))
			++visibleRows;

	const bool empty = m_Ui.CurrentDirectoryExplorer->model() == m_FileModel &&
	                   !m_FileModel->canFetchMore(root) && visibleRows == 0;

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
	const QString asset      = editor::AssetAt(model, index, m_RootPath);

	auto  menu   = QMenu(this);
	auto* addDir = menu.addAction("Add Directory");

	QAction* bake          = nullptr;
	QAction* rename        = nullptr;
	QAction* remove        = nullptr;
	QAction* removeCascade = nullptr;
	if (!asset.isEmpty())
	{
		menu.addSeparator();
		if (editor::IsMaterialAsset(asset))
			bake = menu.addAction("Bake");
		rename        = menu.addAction("Rename");
		remove        = menu.addAction("Delete");
		removeCascade = menu.addAction("Delete Cascade");
	}

	QAction* const chosen = menu.exec(view.viewport()->mapToGlobal(pos));

	if (chosen == addDir)
		m_Operations->AddDirectory(&model, parentPath);
	else if (bake != nullptr && chosen == bake)
		m_Operations->Bake(asset);
	else if (rename != nullptr && chosen == rename)
		m_Operations->Rename(asset);
	else if (remove != nullptr && chosen == remove)
		m_Operations->Delete(asset);
	else if (removeCascade != nullptr && chosen == removeCascade)
		m_Operations->DeleteCascade(asset);
}

void
ContentExplorerWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (editor::AcceptsImportDrop(*event->mimeData()))
		event->acceptProposedAction();
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
	if (m_RootPath.isEmpty())
		return;

	editor::RunImportDrop(this, m_RootPath, *event->mimeData());
	event->acceptProposedAction();
}

void
ContentExplorerWindow::OnDirectoryDeleted(const QString& absolute)
{
	const QString shown = m_FileModel->filePath(m_Ui.CurrentDirectoryExplorer->rootIndex());

	// The trail led into a folder that is gone, so it is dropped rather than walked back into --
	// and going home is not a step Back should offer to undo.
	if (QDir(absolute).relativeFilePath(shown).startsWith(".."))
		return;

	m_History.clear();
	ShowDirectory(m_RootPath);
}

void
ContentExplorerWindow::OnDirectoryRenamed(const QString& fromAbsolute, const QString& toAbsolute)
{
	// Follow the rename rather than dumping the user at the root. The history is left alone -- Back
	// already skips a folder that is gone.
	const QString shown  = m_FileModel->filePath(m_Ui.CurrentDirectoryExplorer->rootIndex());
	const QString inside = QDir(fromAbsolute).relativeFilePath(shown);

	if (inside.startsWith(".."))
		return;

	ShowDirectory(inside == "." ? toAbsolute : toAbsolute + "/" + inside);
}
