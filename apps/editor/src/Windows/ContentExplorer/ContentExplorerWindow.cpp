#include "ContentExplorerWindow.h"

#include "Import/drop_import.h"
#include "Windows/ContentExplorer/AssetOperations.h"
#include "Windows/ContentExplorer/asset_rules.h"
#include "util/asset_paths.h"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
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
#include <assetlib/project_layout.h>

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
	m_Ui = editor::BuildContentExplorerUi(this);

	m_Ui.splitter->setStretchFactor(0, 0);
	m_Ui.splitter->setStretchFactor(1, 1);
	m_Ui.splitter->setSizes({ 220, 700 });

	m_Ui.backButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
	m_Ui.backButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
	connect(m_Ui.backButton, &QToolButton::clicked, this, &ContentExplorerWindow::NavigateBack);

	connect(
		m_Ui.modeSelector,
		&QComboBox::activated,  // activated, not currentIndexChanged: only a user's pick re-roots
		this,
		[this](int index) {
			SetBrowseMode(index == 1 ? editor::BrowseMode::kTextures : editor::BrowseMode::kAssets);
		});

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
			HideUnlistedRows(m_Ui.fileExplorer, *m_HierarchyModel, parent, first, last);
		});
	connect(
		m_FileModel,
		&QAbstractItemModel::rowsInserted,
		this,
		[this](const QModelIndex& parent, int first, int last) {
			HideUnlistedRows(m_Ui.currentDirectory, *m_FileModel, parent, first, last);
		});

	m_Ui.fileExplorer->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(
		m_Ui.fileExplorer,
		&QWidget::customContextMenuRequested,
		this,
		&ContentExplorerWindow::ShowHierarchyMenu);

	m_Ui.currentDirectory->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(
		m_Ui.currentDirectory,
		&QWidget::customContextMenuRequested,
		this,
		&ContentExplorerWindow::ShowFileMenu);

	auto* viewport = m_Ui.currentDirectory->viewport();
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

	// The views root one level in, at the half this mode browses. Everything else here keeps
	// resolving against `m_RootPath`: a key is data-root-relative, so moving where the *views* point
	// must not move what a path means.
	SetBrowseMode(m_Mode);
}

void
ContentExplorerWindow::SetBrowseMode(const editor::BrowseMode mode)
{
	const QString root = editor::GetBrowseRootFor(m_RootPath, mode);
	if (root.isEmpty())
		return;

	// Project::Create and Project::Open scaffold every required directory, this one among them, so
	// an absent root means the project's layout is broken rather than merely empty. Refused rather
	// than repaired: a mode that exists to be read-only must not write to be entered, and papering
	// over a missing required directory hides that the project needs reopening. Refusing matters
	// because an unrooted QFileSystemModel lists the whole filesystem.
	if (!QDir(root).exists())
	{
		qWarning(
			"ContentExplorer: '%s' is missing, so the project's layout is incomplete",
			qPrintable(root));

		m_Ui.modeSelector->setCurrentIndex(static_cast<int>(m_Mode));
		return;
	}

	m_Mode       = mode;
	m_BrowseRoot = root;
	m_History.clear();

	m_Ui.fileExplorer->setRootIndex(m_HierarchyModel->setRootPath(m_BrowseRoot));
	ShowDirectory(m_BrowseRoot);
}

bool
ContentExplorerWindow::IsInsideBrowseRoot(const QString& path) const
{
	return editor::IsKeyUnder(m_BrowseRoot, path);
}

void
ContentExplorerWindow::ShowDirectory(const QString& path)
{
	// The last word on where a view may point, whoever asked. Every caller below reaches here.
	if (!IsInsideBrowseRoot(path))
		return;

	m_Ui.currentDirectory->setRootIndex(m_FileModel->setRootPath(path));
	HideUnlistedRows(m_Ui.currentDirectory, *m_FileModel, m_Ui.currentDirectory->rootIndex());

	// The tree follows, or it would go on highlighting the folder the grid has left -- and clicking
	// that row again would be a dead click, setCurrentIndex on the current index emitting nothing.
	// Not when it is already there: the tree is what navigates most of the time, and pulling its
	// current index onto the folder would take it off the file the user just clicked. Re-entering
	// through currentChanged is harmless -- the grid is rooted above, so NavigateTo returns early.
	if (QDir(m_HierarchyModel->filePath(
			FolderOf(*m_HierarchyModel, m_Ui.fileExplorer->currentIndex()))) != QDir(path))
	{
		// Cleared rather than set at the top of the tree, which is a root the tree has no row for.
		const QModelIndex folder = m_HierarchyModel->index(path);
		m_Ui.fileExplorer->setCurrentIndex(
			folder == m_Ui.fileExplorer->rootIndex() ? QModelIndex() : folder);
	}

	m_Ui.backButton->setEnabled(!m_History.isEmpty());
	UpdateEmptyPlaceholder();
}

void
ContentExplorerWindow::NavigateTo(const QString& path)
{
	// Before the history, not just before the move: recording a step Back can never return to
	// would make the button lie about where it goes.
	if (!IsInsideBrowseRoot(path))
		return;

	const QString shown = m_FileModel->filePath(m_Ui.currentDirectory->rootIndex());

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

	m_Ui.backButton->setEnabled(false);
}

void
ContentExplorerWindow::HideUnlistedRows(
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
		if (!editor::IsHiddenInExplorer(model.filePath(model.index(row, 0, parent))))
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
	if (m_Ui.fileExplorer->model() == m_HierarchyModel)
		return;

	m_Ui.fileExplorer->setModel(m_HierarchyModel);
	m_Ui.fileExplorer->setHeaderHidden(true);
	connect(m_Ui.fileExplorer, &QTreeView::expanded, this, [this](const QModelIndex& parent) {
		HideUnlistedRows(m_Ui.fileExplorer, *m_HierarchyModel, parent);
	});
	for (auto column = 1; column < m_HierarchyModel->columnCount(); ++column)
		m_Ui.fileExplorer->hideColumn(column);

	m_Ui.currentDirectory->setModel(m_FileModel);
	m_Ui.currentDirectory->setEditTriggers(QAbstractItemView::NoEditTriggers);

	// A grid of tiles, each an asset's thumbnail above its name.
	m_Ui.currentDirectory->setViewMode(QListView::IconMode);
	m_Ui.currentDirectory->setIconSize(QSize(c_TileIconDim, c_TileIconDim));
	m_Ui.currentDirectory->setGridSize(QSize(c_TileWidth, c_TileHeight));
	m_Ui.currentDirectory->setResizeMode(QListView::Adjust);
	m_Ui.currentDirectory->setUniformItemSizes(true);
	m_Ui.currentDirectory->setWordWrap(true);

	// IconMode lets the user shuffle tiles around the grid by default, which would imply an ordering
	// the folder does not have.
	m_Ui.currentDirectory->setMovement(QListView::Static);

	// Assets can be dragged out of the explorer (e.g. a .bmesh onto the Material Editor preview).
	// QFileSystemModel supplies the file URLs; DragOnly keeps the views from accepting drops, so
	// dropped mesh files still bubble up to this widget's dropEvent for import.
	m_Ui.fileExplorer->setDragEnabled(true);
	m_Ui.fileExplorer->setDragDropMode(QAbstractItemView::DragOnly);
	m_Ui.currentDirectory->setDragEnabled(true);
	m_Ui.currentDirectory->setDragDropMode(QAbstractItemView::DragOnly);

	// Selecting an entry on the left shows the containing folder's contents on the right. The tree
	// lists files too, and a file is not a directory to root the right-hand view at, so selecting one
	// shows the folder it lives in.
	connect(
		m_Ui.fileExplorer->selectionModel(),
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
		m_Ui.currentDirectory,
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
	m_Ui.fileExplorer->setModel(nullptr);
	m_Ui.currentDirectory->setModel(nullptr);
	m_EmptyPlaceholder->hide();
	m_History.clear();
	m_Ui.backButton->setEnabled(false);
	setEnabled(false);
}

void
ContentExplorerWindow::UpdateEmptyPlaceholder()
{
	auto* viewport = m_Ui.currentDirectory->viewport();

	// Only meaningful once a folder is shown; and while the model is still fetching the
	// directory's contents we can't yet tell whether it's empty, so wait for the reload.
	const auto root = m_Ui.currentDirectory->rootIndex();

	// Counting what the view shows, not what the model holds: a folder of nothing but hidden
	// sidecars is empty to the user.
	auto visibleRows = 0;
	for (int row = 0; row < m_FileModel->rowCount(root); ++row)
		if (!m_Ui.currentDirectory->isRowHidden(row))
			++visibleRows;

	const bool empty = m_Ui.currentDirectory->model() == m_FileModel &&
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
	if (watched == m_Ui.currentDirectory->viewport() && event->type() == QEvent::Resize)
	{
		m_EmptyPlaceholder->setGeometry(m_Ui.currentDirectory->viewport()->rect());
	}

	return QWidget::eventFilter(watched, event);
}

void
ContentExplorerWindow::ShowHierarchyMenu(const QPoint& pos)
{
	if (m_Ui.fileExplorer->model() != m_HierarchyModel)
		return;

	ShowAssetMenu(*m_Ui.fileExplorer, *m_HierarchyModel, pos);
}

void
ContentExplorerWindow::ShowFileMenu(const QPoint& pos)
{
	if (m_Ui.currentDirectory->model() != m_FileModel)
		return;

	ShowAssetMenu(*m_Ui.currentDirectory, *m_FileModel, pos);
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

	// Read-only modes offer no menu at all rather than a menu of refusals: IsActionableAsset would
	// turn every one of these into a no-op, and an item that does nothing reads as a broken one.
	if (!editor::IsEditableMode(m_Mode))
		return;

	auto  menu   = QMenu(this);
	auto* addDir = menu.addAction("Add Directory");

	QAction* bake          = nullptr;
	QAction* rename        = nullptr;
	QAction* remove        = nullptr;
	QAction* removeCascade = nullptr;
	if (editor::IsActionableAsset(asset))
	{
		menu.addSeparator();
		if (editor::IsMaterialAsset(asset))
			bake = menu.addAction("Bake");
		rename = menu.addAction("Rename");

		// An imported source renames -- moving everything it produced with it -- but does not
		// delete: `planDeletion` throws for one, and grouped deletion is ADR-8's non-goal.
		if (editor::IsRemovableAsset(asset))
		{
			remove        = menu.addAction("Delete");
			removeCascade = menu.addAction("Delete Cascade");
		}
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
	if (editor::IsEditableMode(m_Mode) && editor::AcceptsImportDrop(*event->mimeData()))
		event->acceptProposedAction();
}

void
ContentExplorerWindow::dragMoveEvent(QDragMoveEvent* event)
{
	// The accept decision doesn't depend on position, so mirror dragEnterEvent.
	if (editor::IsEditableMode(m_Mode) && editor::AcceptsImportDrop(*event->mimeData()))
		event->acceptProposedAction();
}

void
ContentExplorerWindow::dropEvent(QDropEvent* event)
{
	if (m_RootPath.isEmpty() || !editor::IsEditableMode(m_Mode))
		return;

	editor::RunImportDrop(this, m_RootPath, *event->mimeData());
	event->acceptProposedAction();
}

void
ContentExplorerWindow::OnDirectoryDeleted(const QString& absolute)
{
	const QString shown = m_FileModel->filePath(m_Ui.currentDirectory->rootIndex());

	// The trail led into a folder that is gone, so it is dropped rather than walked back into --
	// and going home is not a step Back should offer to undo.
	if (!editor::IsKeyUnder(absolute, shown))
		return;

	m_History.clear();
	ShowDirectory(m_BrowseRoot);
}

void
ContentExplorerWindow::OnDirectoryRenamed(const QString& fromAbsolute, const QString& toAbsolute)
{
	// Follow the rename rather than dumping the user at the root. The history is left alone -- Back
	// already skips a folder that is gone.
	const QString shown  = m_FileModel->filePath(m_Ui.currentDirectory->rootIndex());
	const QString inside = editor::GetKeyUnder(fromAbsolute, shown);

	if (inside.isEmpty())
		return;

	ShowDirectory(inside == "." ? toAbsolute : toAbsolute + "/" + inside);
}
