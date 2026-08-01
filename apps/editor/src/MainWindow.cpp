#include "MainWindow.h"

#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>

#include "Async/BackgroundTask.h"
#include "Project/Project.h"
#include "Render/Renderer.h"
#include "Thumbnails/AssetThumbnailCache.h"
#include "Windows/ContentExplorer/ContentExplorerWindow.h"
#include "Windows/LevelEditor/LevelEditorWindow.h"
#include "Windows/MaterialEditor/MaterialEditorWindow.h"
#include <assetlib/texture_prune.h>
#include <bgl/IGraphics.h>
#include <core/file/file.h>
#include <core/settings/Settings.h>
#include <gamelib/AssetManager.h>

namespace
{
	// A leading ~ is what a person writes in a config file and what no filesystem API expands.
	std::filesystem::path
	ExpandHome(const std::string& value)
	{
		if (!value.starts_with("~/") && value != "~")
			return std::filesystem::path(value);

		const char* home = std::getenv("HOME");
		if (home == nullptr)
			home = std::getenv("USERPROFILE");
		if (home == nullptr)
			return std::filesystem::path(value);

		return std::filesystem::path(home) / value.substr(value.size() > 1 ? 2 : 1);
	}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
	m_Ui.setupUi(this);

	connect(m_Ui.actionNewProject, &QAction::triggered, this, &MainWindow::NewProject);
	connect(m_Ui.actionOpenProject, &QAction::triggered, this, &MainWindow::OpenProject);
	connect(
		m_Ui.actionCleanUnusedTextures,
		&QAction::triggered,
		this,
		&MainWindow::CleanUnusedTextures);
	connect(m_Ui.actionExit, &QAction::triggered, this, &QWidget::close);

	std::string startupProject;
	{
		const auto     configPath = core::file::get_executable_path().parent_path() / "config.json";
		core::Settings settings(configPath);

		startupProject         = settings["startupProject"].GetOrDefault(std::string());
		const auto gfxSettings = settings["graphics"];

		auto gfxOpts             = bgl::GraphicsOptions();
		gfxOpts.enableDebugLayer = gfxSettings["enableDebugLayer"].GetOrDefault(false);
		gfxOpts.enableGPUValidationLayer =
			gfxSettings["enableGPUBasedValidation"].GetOrDefault(false);
		gfxOpts.enablePixDebug = gfxSettings["enablePixDebug"].GetOrDefault(false);
		gfxOpts.strictError    = gfxSettings["strictError"].GetOrDefault(false);
		gfxOpts.logLevel       = static_cast<bgl::GraphicsOptions::LogLevel>(
			gfxSettings["logLevel"].GetOrDefault(static_cast<int>(gfxOpts.logLevel)));
		gfxOpts.maxCbvSrvUavs = gfxSettings["maxCbvSrvUavs"].GetOrDefault(gfxOpts.maxCbvSrvUavs);
		gfxOpts.maxRtvs       = gfxSettings["maxRtvs"].GetOrDefault(gfxOpts.maxRtvs);
		gfxOpts.maxDsvs       = gfxSettings["maxDsvs"].GetOrDefault(gfxOpts.maxDsvs);
		gfxOpts.maxTextures   = gfxSettings["maxTextures"].GetOrDefault(gfxOpts.maxTextures);

		if (gfxSettings["enableShaderCache"].GetOrDefault(true))
			gfxOpts.shaderCacheDir = "shadercache";

		// The editor's one Scene. Every viewport (the Level Editor, the Material Editor's model
		// preview) renders it through a SceneView of its own, so geometry, textures and materials
		// are pooled here once and these budgets must cover all of them together.
		auto sceneDesc             = bgl::SceneDesc();
		auto sceneSettings         = settings["scene"];
		sceneDesc.initialGeom      = sceneSettings["initialGeom"].GetOrDefault(256);
		sceneDesc.initialMeshlets  = sceneSettings["initialMeshlets"].GetOrDefault(32768);
		sceneDesc.initialSubmeshes = sceneSettings["initialSubmeshes"].GetOrDefault(512);
		sceneDesc.initialVertexBufferByteSize =
			sceneSettings["initialVertexBufferByteSize"].GetOrDefault(33554432);
		sceneDesc.initialIndices      = sceneSettings["initialIndices"].GetOrDefault(2000000);
		sceneDesc.initialPbrMaterials = sceneSettings["initialPbrMaterials"].GetOrDefault(256);
		sceneDesc.initialLoosePbrMaterials =
			sceneSettings["initialLoosePbrMaterials"].GetOrDefault(256);

		// The renderer owns the Graphics and the Scene and, once threaded, is the only thing that
		// touches them. Every viewport and the thumbnail cache render through it.
		m_Renderer = std::make_unique<Renderer>(gfxOpts, sceneDesc);

		auto levelDesc             = RenderTargetWindowDesc();
		levelDesc.renderer         = m_Renderer.get();
		levelDesc.initialInstances = settings["levelEditor"]["initialInstances"].GetOrDefault(1000);

		m_LevelEditor = new LevelEditorWindow(this, std::move(levelDesc));

		auto matSettings                = settings["materialEditor"];
		auto matDesc                    = MaterialEditorWindowDesc();
		matDesc.renderer                = m_Renderer.get();
		matDesc.initialPreviewInstances = matSettings["initialPreviewInstances"].GetOrDefault(16u);
		matDesc.previewEnv.environmentMap =
			matSettings["environmentMap"].GetOrDefault(std::string());
		matDesc.previewEnv.brdfLut = matSettings["brdfLut"].GetOrDefault(std::string());

		// Absent, and the .benv's own derived exposure stands -- which is the correct one for its maps.
		if (auto exposure = matSettings["exposure"])
			matDesc.previewEnv.exposureOverride = exposure.GetOrDefault(1.0f);

		auto thumbSettings         = settings["thumbnails"];
		auto thumbDesc             = AssetThumbnailDesc();
		thumbDesc.renderer         = m_Renderer.get();
		thumbDesc.dimension        = thumbSettings["dimension"].GetOrDefault(256u);
		thumbDesc.initialInstances = thumbSettings["initialInstances"].GetOrDefault(256u);
		thumbDesc.environmentMap   = thumbSettings["environmentMap"].GetOrDefault(std::string());
		thumbDesc.brdfLut          = thumbSettings["brdfLut"].GetOrDefault(std::string());

		if (auto exposure = thumbSettings["exposure"])
			thumbDesc.exposureOverride = exposure.GetOrDefault(1.0f);

		m_MaterialEditor = new MaterialEditorWindow(this, std::move(matDesc));
		m_Thumbnails     = std::make_unique<AssetThumbnailCache>(std::move(thumbDesc));
	}

	setDockNestingEnabled(true);
	setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

	m_LevelEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_LevelEditor->setMinimumSize(256, 256);

	m_LevelEditorDock = new QDockWidget("Level Editor", this);
	m_LevelEditorDock->setObjectName("LevelEditorDock");
	m_LevelEditorDock->setWidget(m_LevelEditor);
	m_LevelEditorDock->setTitleBarWidget(new QWidget(m_LevelEditorDock));
	addDockWidget(Qt::TopDockWidgetArea, m_LevelEditorDock);

	m_MaterialEditorDock = new QDockWidget("Material Editor", this);
	m_MaterialEditorDock->setObjectName("MaterialEditorDock");
	m_MaterialEditorDock->setWidget(m_MaterialEditor);
	m_MaterialEditorDock->setTitleBarWidget(new QWidget(m_MaterialEditorDock));
	addDockWidget(Qt::TopDockWidgetArea, m_MaterialEditorDock);

	tabifyDockWidget(m_LevelEditorDock, m_MaterialEditorDock);

	m_ContentExplorerDock = new QDockWidget("Content Explorer", this);
	m_ContentExplorerDock->setObjectName("ContentExplorerDock");

	// The explorer refuses to delete a material the Material Editor has open, whose next Save would
	// write it straight back. Asked at each deletion, so there is no copy of the answer to go stale.
	m_ContentExplorer = new ContentExplorerWindow(m_ContentExplorerDock, [this] {
		return m_MaterialEditor->OpenMaterialPaths();
	});
	m_ContentExplorer->SetThumbnails(m_Thumbnails.get());

	// Baking rewrites the material on disk, which is where the Material Editor's panel reads the
	// staleness marker and the baked-texture listing from.
	connect(
		m_ContentExplorer,
		&ContentExplorerWindow::MaterialBaked,
		m_MaterialEditor,
		&MaterialEditorWindow::RefreshMaterialState);

	m_ContentExplorer->setMinimumSize(0, 0);
	m_ContentExplorerDock->setWidget(m_ContentExplorer);
	addDockWidget(Qt::BottomDockWidgetArea, m_ContentExplorerDock);

	m_Ui.menuWindow->addAction(m_LevelEditorDock->toggleViewAction());
	m_Ui.menuWindow->addAction(m_MaterialEditorDock->toggleViewAction());
	m_Ui.menuWindow->addAction(m_ContentExplorerDock->toggleViewAction());

	SetUpFrameStats();

	// config.json may name a project to open on launch, so working on one does not mean reopening
	// it every run. It is machine-local (the file is git-ignored), which is what makes naming an
	// absolute path in it reasonable.
	if (startupProject.empty() || !OpenProjectAt(ExpandHome(startupProject)))
		ShowEmptyState();
}

MainWindow::~MainWindow()
{
	// Everything that renders releases its bgl objects through the Renderer, so all of it has to be
	// gone before the Renderer member is. Qt would otherwise destroy these as children of this window,
	// which happens after the members -- with the render thread already stopped.
	m_ContentExplorer->SetThumbnails(nullptr);
	m_Thumbnails.reset();

	// After the thumbnails, which release their materials back through it, and before the viewports,
	// so the instances it deletes leave views that are still standing.
	m_Renderer->Invoke([&] { m_Assets.reset(); });

	delete m_LevelEditor;
	delete m_MaterialEditor;
}

void
MainWindow::NewProject()
{
	const auto name = QInputDialog::getText(this, "New Project", "Project name:").trimmed();
	if (name.isEmpty())
		return;

	const auto location = QFileDialog::getExistingDirectory(this, "Select Project Location");
	if (location.isEmpty())
		return;

	const auto root        = std::filesystem::path(location.toStdWString()) / name.toStdString();
	const auto projectFile = root / (name.toStdString() + Project::c_FileExtension);

	try
	{
		SetActiveProject(Project::Create(projectFile, name.toStdString()));
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(this, "New Project", e.what());
	}
}

void
MainWindow::OpenProject()
{
	const auto filter =
		QString("Bernini Project (*%1)").arg(QString::fromUtf8(Project::c_FileExtension));
	const auto file = QFileDialog::getOpenFileName(this, "Open Project", QString(), filter);
	if (file.isEmpty())
		return;

	OpenProjectAt(std::filesystem::path(file.toStdWString()));
}

bool
MainWindow::OpenProjectAt(const std::filesystem::path& path)
{
	try
	{
		SetActiveProject(Project::Open(path));
		return true;
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(this, "Open Project", e.what());
		return false;
	}
}

void
MainWindow::CleanUnusedTextures()
{
	if (!m_Project)
		return;

	auto desc     = assetlib::TexturePruneDesc();
	desc.dataRoot = m_Project->GetDataDirectory();

	auto scan = assetlib::TexturePruneScan();

	// Scanning parses every .bmaterial in the project, so it runs off the UI thread. It reads assetlib
	// only, never bgl, which is what the loading screen requires of its worker. findUnusedBakedTextures
	// takes no cancel token, so the screen offers no button that would not work.
	const background::TaskResult scanned = background::RunWithLoadingScreen(
		this,
		"Clean Unused Textures",
		[&](background::Progress& progress) {
			progress.Report(0, 0, "Scanning materials...");
			scan = assetlib::findUnusedBakedTextures(desc);
		});

	if (!scanned.Completed())
	{
		QMessageBox::warning(
			this,
			"Clean Unused Textures",
			QString("Could not scan the project:\n\n%1").arg(scanned.error));
		return;
	}

	const auto formatSize = [](uint64_t bytes) {
		return QLocale().formattedDataSize(static_cast<qint64>(bytes));
	};

	if (scan.unused.empty())
	{
		QMessageBox::information(
			this,
			"Clean Unused Textures",
			QString(
				"No unused baked textures.\n\n%1 of the %2 baked textures are referenced by the "
				"project's %3 materials.")
				.arg(scan.liveMaps)
				.arg(scan.candidates)
				.arg(scan.materialsScanned));
		return;
	}

	auto details = QStringList();
	for (const assetlib::UnusedTexture& texture : scan.unused)
		details << QString::fromStdString(texture.path);

	auto confirm = QMessageBox(this);
	confirm.setWindowTitle("Clean Unused Textures");
	confirm.setIcon(QMessageBox::Warning);
	confirm.setText(QString("Delete %1 unused baked textures?")
	                    .arg(static_cast<qulonglong>(scan.unused.size())));
	confirm.setInformativeText(
		QString(
			"No material in this project references them; %1 will be reclaimed.\n\nThis cannot be "
			"undone, but a deleted map is rebuilt by re-baking the material that needs it.")
			.arg(formatSize(scan.bytes)));
	confirm.setDetailedText(details.join('\n'));

	auto* deleteButton = confirm.addButton("Delete", QMessageBox::DestructiveRole);
	confirm.addButton(QMessageBox::Cancel);
	confirm.setDefaultButton(QMessageBox::Cancel);
	confirm.exec();

	if (confirm.clickedButton() != deleteButton)
		return;

	// Unlinking is fast, so it stays on the UI thread; the scan is what was slow.
	const auto result = assetlib::deleteUnusedBakedTextures(scan, desc);

	if (!result.failed.empty())
	{
		QMessageBox::warning(
			this,
			"Clean Unused Textures",
			QString("Deleted %1 textures, but %2 could not be removed:\n\n%3")
				.arg(static_cast<qulonglong>(result.deleted))
				.arg(static_cast<qulonglong>(result.failed.size()))
				.arg(QString::fromStdString(result.failed.front())));
		return;
	}

	statusBar()->showMessage(
		QString("Deleted %1 unused baked textures, reclaiming %2")
			.arg(static_cast<qulonglong>(result.deleted))
			.arg(formatSize(result.bytes)),
		5000);
}

void
MainWindow::SetActiveProject(Project project)
{
	m_Project = std::make_unique<Project>(std::move(project));

	const auto dataDir = QString::fromStdWString(m_Project->GetDataDirectory().wstring());

	// A manager resolves every path against one Data root, so a new project needs a new one. The
	// consumers below borrow it, so it has to be replaced before any of them are told about it.
	if (m_Thumbnails)
		m_Thumbnails->SetAssets(nullptr);

	// ~AssetManager hands every asset it still holds back to the scene, so it runs on the render
	// thread like any other scene mutation -- the viewports are still drawing at this point.
	m_Renderer->Invoke([&] { m_Assets.reset(); });

	// One manager over the editor's one scene: every viewport draws that scene, so a texture a material
	// shares is one upload and one reference count no matter which view shows it. Each view names itself
	// when it places an instance.
	m_Assets =
		std::make_unique<game::AssetManager>(m_Renderer->GetScene(), m_Project->GetDataDirectory());

	// Hand it over before the explorer is rooted: rooting it paints tiles, and each one that misses
	// asks for a render straight away -- a material cannot be resolved without a manager.
	if (m_Thumbnails)
		m_Thumbnails->SetAssets(m_Assets.get());

	m_ContentExplorer->SetRootPath(dataDir);

	if (m_MaterialEditor)
	{
		// Root first, then reset: the reset repopulates the preview, which resolves the material
		// paths it finds against the data root.
		m_MaterialEditor->SetDataRoot(dataDir);
		m_MaterialEditor->Reset();
	}

	ShowProjectState();

	setWindowTitle(
		QString("Bernini Editor — %1").arg(QString::fromStdString(m_Project->GetName())));
	statusBar()->showMessage(
		QString("Project data: %1")
			.arg(QString::fromStdString(m_Project->GetDataDirectory().string())));
}

void
MainWindow::SetUpFrameStats()
{
	if (m_LevelEditor == nullptr)
		return;

	m_FrameStats = new QLabel(this);
	m_FrameStats->setObjectName("FrameStats");
	m_FrameStats->setToolTip(
		"Level Editor frame time: mean and worst over the last 120 frames, and how many of them "
		"overran a vblank.");

	// A permanent widget sits to the right of the bar and survives showMessage, so the project and
	// texture-cleanup messages cannot overwrite the readout.
	statusBar()->addPermanentWidget(m_FrameStats);

	// Queued: FrameStatsUpdated is emitted on the render thread and this touches a widget.
	connect(
		m_LevelEditor,
		&RenderTargetWindow::FrameStatsUpdated,
		this,
		[this](double meanMs, double maxMs, int missed) {
			m_FrameStats->setText(
				QString::asprintf(
					"frame %.1f ms avg  %.1f ms max  %d missed",
					meanMs,
					maxMs,
					missed));
		},
		Qt::QueuedConnection);
}

void
MainWindow::ShowEmptyState()
{
	m_LevelEditorDock->hide();
	m_MaterialEditorDock->hide();
	m_ContentExplorerDock->hide();

	m_Ui.actionSave->setEnabled(false);
	m_Ui.actionCleanUnusedTextures->setEnabled(false);
	m_Ui.menuEdit->setEnabled(false);
	m_Ui.menuWindow->setEnabled(false);

	auto* placeholder = new QLabel(
		"Open a project to get started.\n\nFile ▸ New Project…   or   File ▸ Open Project…",
		this);
	placeholder->setObjectName("EmptyStatePlaceholder");
	placeholder->setAlignment(Qt::AlignCenter);
	placeholder->setEnabled(false);

	setCentralWidget(placeholder);
}

void
MainWindow::ShowProjectState()
{
	setCentralWidget(nullptr);

	m_LevelEditorDock->show();
	m_MaterialEditorDock->show();
	m_ContentExplorerDock->show();
	m_LevelEditorDock->raise();

	m_Ui.actionSave->setEnabled(true);
	m_Ui.actionCleanUnusedTextures->setEnabled(true);
	m_Ui.menuEdit->setEnabled(true);
	m_Ui.menuWindow->setEnabled(true);

	resizeDocks({ m_LevelEditorDock, m_ContentExplorerDock }, { 700, 220 }, Qt::Vertical);
}
