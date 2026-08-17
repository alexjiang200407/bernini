#include "ContentExplorerWindow.h"

#include "Async/BackgroundTask.h"
#include "Import/drop_import.h"
#include "Project/Project.h"
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
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>

#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/material_bake.h>
#include <assetlib_structs/BMaterial.h>

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
	QWidget(parent), m_AssetsHeldOpen(std::move(assetsHeldOpen))
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
		AddDirectory(&model, parentPath);
	else if (bake != nullptr && chosen == bake)
		BakeMaterial(asset);
	else if (rename != nullptr && chosen == rename)
		RenameAsset(asset);
	else if (remove != nullptr && chosen == remove)
		DeleteAsset(asset);
	else if (removeCascade != nullptr && chosen == removeCascade)
		DeleteAssetCascade(asset);
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

bool
ContentExplorerWindow::IsHeldOpen(const QString& absolute, bool isDirectory) const
{
	const auto holds = [&](const QString& open) {
		if (!isDirectory)
			return QFileInfo(open) == QFileInfo(absolute);

		return !QDir(absolute).relativeFilePath(open).startsWith("..");
	};

	return std::ranges::any_of(m_AssetsHeldOpen(), holds);
}

void
ContentExplorerWindow::DeleteAsset(const QString& asset)
{
	DeleteWithPlanner(asset, assetlib::planDeletion);
}

void
ContentExplorerWindow::DeleteAssetCascade(const QString& asset)
{
	DeleteWithPlanner(asset, assetlib::planCascadeDeletion);
}

void
ContentExplorerWindow::DeleteWithPlanner(
	const QString& asset,
	assetlib::DeletionPlan (*planner)(const assetlib::AssetRefGraph&, std::string_view))
{
	const QString absolute    = QDir(m_RootPath).absoluteFilePath(asset);
	const bool    isDirectory = QFileInfo(absolute).isDir();

	if (IsHeldOpen(absolute, isDirectory))
	{
		QMessageBox::warning(
			this,
			"Delete",
			QString(
				"%1 is open in an editor panel.\n\nClose it there first: the Material Editor's "
				"next Save would write it back, and the Animation panel would go on offering it.")
				.arg(
					isDirectory ? QString("'%1' holds an asset that").arg(asset) :
								  QString("'%1'").arg(asset)));
		return;
	}

	// Built inside the worker, not beside it: an AssetStore over a root that has gone throws, and
	// out here that would leave a Qt slot rather than the loading screen's error.
	auto store = std::optional<assetlib::AssetStore>();
	auto graph = std::optional<assetlib::AssetRefGraph>();

	// The scan parses every mesh and material in the project, so it runs off the UI thread. It reads
	// assetlib only, never bgl, which is what the loading screen requires of its worker. It takes no
	// cancel token, so the screen offers no button that would not work.
	const background::TaskResult scanned =
		background::RunWithLoadingScreen(this, "Delete", [&](background::Progress& progress) {
			progress.Report(0, 0, "Checking references...");
			store.emplace(std::filesystem::path(m_RootPath.toStdWString()));
			graph = assetlib::AssetRefGraph::Scan(*store);
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

	const assetlib::DeletionPlan plan = planner(*graph, asset.toStdString());

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

	// The cascade takes files the user did not click, so an open one blocks it for the reason the
	// target itself would: a panel still holding it would write it back or go on offering it.
	for (const std::string& freed : plan.cascade)
	{
		const QString member = QString::fromStdString(freed);
		if (!IsHeldOpen(QDir(m_RootPath).absoluteFilePath(member), false))
			continue;

		QMessageBox::warning(
			this,
			"Delete",
			QString(
				"'%1' would be deleted with '%2', but it is open in an editor "
				"panel.\n\nClose it there first.")
				.arg(member, asset));
		return;
	}

	auto contents = QStringList();
	for (const std::string& file : plan.contents) contents << QString::fromStdString(file);

	auto cascade = QStringList();
	for (const std::string& file : plan.cascade) cascade << QString::fromStdString(file);

	auto confirm = QMessageBox(this);
	confirm.setWindowTitle("Delete");
	confirm.setIcon(QMessageBox::Warning);

	if (plan.IsDirectory())
	{
		confirm.setText(QString("Delete '%1' and everything in it?").arg(asset));

		QString info = contents.isEmpty() ?
		                   QString("The folder is empty.") :
		                   QString(
							   "%1 file(s) will be deleted. Nothing outside the folder references "
							   "any of them.")
		                       .arg(contents.size());
		if (!cascade.isEmpty())
			info += QString(
						"\n\n%1 asset(s) outside the folder are referenced only from inside it, "
						"and will be deleted too.")
			            .arg(cascade.size());

		confirm.setInformativeText(info + "\n\nThis cannot be undone.");
		confirm.setDetailedText((contents + cascade).join('\n'));
	}
	else if (!cascade.isEmpty())
	{
		confirm.setText(QString("Delete '%1'?").arg(asset));
		confirm.setInformativeText(
			QString(
				"Nothing references it. %1 asset(s) that nothing else references will be deleted "
				"with it.\n\nThis cannot be undone.")
				.arg(cascade.size()));
		confirm.setDetailedText(cascade.join('\n'));
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

	const assetlib::DeletionResult result = assetlib::deleteAsset(plan, *store);

	switch (result.status)
	{
	case assetlib::DeletionStatus::kDeleted:
		// The model watches the directory, so the row goes on its own -- but a view rooted *inside* what
		// just went is left showing a folder that no longer exists, and has nowhere to navigate back to.
		if (isDirectory)
		{
			const QString shown = m_FileModel->filePath(m_Ui.CurrentDirectoryExplorer->rootIndex());

			// The trail led into a folder that is gone, so it is dropped rather than walked back
			// into -- and going home is not a step Back should offer to undo.
			if (!QDir(absolute).relativeFilePath(shown).startsWith(".."))
			{
				m_History.clear();
				ShowDirectory(m_RootPath);
			}
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
ContentExplorerWindow::RenameAsset(const QString& asset)
{
	const QString   absolute = QDir(m_RootPath).absoluteFilePath(asset);
	const QFileInfo info(absolute);
	const bool      isDirectory = info.isDir();

	// The extension is not offered for editing: it says what the asset is, and every stored
	// reference and menu action dispatches on it.
	const QString stem = isDirectory ? info.fileName() : info.completeBaseName();

	bool    ok      = false;
	QString entered = QInputDialog::getText(
						  this,
						  "Rename",
						  isDirectory ? "Directory name:" : "Name:",
						  QLineEdit::Normal,
						  stem,
						  &ok)
	                      .trimmed();

	// The dialog edits the stem, but a user asked for a file's name types the extension back readily
	// enough -- taken literally that would yield 'Body.bmaterial.bmaterial'.
	const QString suffix = isDirectory ? QString() : "." + info.suffix();
	if (!suffix.isEmpty() && entered.endsWith(suffix, Qt::CaseInsensitive))
		entered.chop(suffix.size());

	if (!ok || entered.isEmpty() || entered == stem)
		return;

	if (!editor::IsValidAssetFileName(entered))
	{
		QMessageBox::warning(
			this,
			"Rename",
			QString("'%1' is not a name every platform this project is shared with can use.")
				.arg(entered));
		return;
	}

	if (IsHeldOpen(absolute, isDirectory))
	{
		QMessageBox::warning(
			this,
			"Rename",
			QString(
				"%1 is open in an editor panel.\n\nClose it there first: the Material Editor's "
				"next Save would write the old name back, and the Animation panel would go on "
				"offering it.")
				.arg(
					isDirectory ? QString("'%1' holds an asset that").arg(asset) :
								  QString("'%1'").arg(asset)));
		return;
	}

	const QString newName = isDirectory ? entered : entered + "." + info.suffix();
	const int     slash   = asset.lastIndexOf('/');
	const QString to      = slash < 0 ? newName : asset.left(slash + 1) + newName;

	// Built inside the worker, not beside it: an AssetStore over a root that has gone throws, and
	// out here that would leave a Qt slot rather than the loading screen's error.
	auto store = std::optional<assetlib::AssetStore>();
	auto graph = std::optional<assetlib::AssetRefGraph>();

	// Off the UI thread for the reason Delete's scan is: it parses every mesh and material in the
	// project, reading assetlib alone.
	const background::TaskResult scanned =
		background::RunWithLoadingScreen(this, "Rename", [&](background::Progress& progress) {
			progress.Report(0, 0, "Checking references...");
			store.emplace(std::filesystem::path(m_RootPath.toStdWString()));
			graph = assetlib::AssetRefGraph::Scan(*store);
		});

	if (!scanned.Completed())
	{
		// A mesh or material that will not parse aborts the scan: its references cannot be known, and
		// one of them may name the file about to move -- and would then be left pointing at nothing.
		QMessageBox::warning(
			this,
			"Rename",
			QString("Could not work out what references '%1', so it was not renamed:\n\n%2")
				.arg(asset, scanned.error));
		return;
	}

	auto plan = assetlib::RenamePlan();
	try
	{
		plan = assetlib::planRename(*graph, asset.toStdString(), to.toStdString());
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(
			this,
			"Rename",
			QString("'%1' cannot be renamed:\n\n%2").arg(asset, QString::fromUtf8(e.what())));
		return;
	}

	// The rewrite touches files the user did not click, and an open one is held for the reason the
	// target itself would be: a re-save or a stale offer under the old path.
	auto referrers = QStringList();
	for (const assetlib::AssetRef& ref : plan.referrers)
		referrers << QString::fromStdString(ref.referrer);
	referrers.removeDuplicates();
	referrers.sort();

	for (const QString& referrer : referrers)
	{
		if (!IsHeldOpen(QDir(m_RootPath).absoluteFilePath(referrer), false))
			continue;

		QMessageBox::warning(
			this,
			"Rename",
			QString(
				"'%1' references it and is open in an editor panel.\n\nClose it there "
				"first.")
				.arg(referrer));
		return;
	}

	if (!referrers.isEmpty())
	{
		auto confirm = QMessageBox(this);
		confirm.setWindowTitle("Rename");
		confirm.setIcon(QMessageBox::Question);
		confirm.setText(QString("Rename '%1' to '%2'?").arg(asset, to));
		confirm.setInformativeText(
			QString("%1 asset(s) reference it, and will be rewritten to the new name.")
				.arg(referrers.size()));
		confirm.setDetailedText(referrers.join('\n'));

		auto* apply = confirm.addButton("Rename", QMessageBox::AcceptRole);
		confirm.addButton(QMessageBox::Cancel);
		confirm.setDefaultButton(apply);
		confirm.exec();

		if (confirm.clickedButton() != apply)
			return;
	}

	auto result = assetlib::RenameResult();

	// A worker for the reason the scan gets one, only more so: rewriting a referrer round-trips the
	// whole mesh, geometry and all, where the scan read its material chunk alone. Files only, no bgl.
	const background::TaskResult renamed = background::RunWithLoadingScreen(
		this,
		QString("Renaming %1").arg(QFileInfo(asset).fileName()),
		[&](background::Progress& progress) {
			progress.Report(0, 0, "Rewriting references...");
			result = assetlib::renameAsset(plan, *store);
		});

	if (!renamed.Completed())
	{
		QMessageBox::warning(
			this,
			"Rename",
			QString("'%1' could not be renamed:\n\n%2").arg(asset, renamed.error));
		return;
	}

	if (result.status == assetlib::RenameStatus::kFailed)
	{
		QMessageBox::warning(
			this,
			"Rename",
			QString("'%1' could not be renamed:\n\n%2\n\nIt may be open in another program.")
				.arg(asset, QString::fromStdString(result.error)));
		return;
	}

	// A grid rooted at or inside the renamed folder is left showing a path that no longer exists;
	// follow the rename rather than dumping the user at the root. The history is left alone -- Back
	// already skips a folder that is gone.
	if (isDirectory)
	{
		const QString shown  = m_FileModel->filePath(m_Ui.CurrentDirectoryExplorer->rootIndex());
		const QString inside = QDir(absolute).relativeFilePath(shown);

		if (!inside.startsWith(".."))
		{
			const QString moved = QDir(m_RootPath).absoluteFilePath(to);
			ShowDirectory(inside == "." ? moved : moved + "/" + inside);
		}
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
	if (editor::import::AcceptsDrop(*event->mimeData()))
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

	editor::import::RunDrop(this, m_RootPath, *event->mimeData());
	event->acceptProposedAction();
}
