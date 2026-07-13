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
#include "Windows/ContentExplorer/ContentExplorerWindow.h"
#include "Windows/LevelEditor/LevelEditorWindow.h"
#include "Windows/MaterialEditor/MaterialEditorWindow.h"
#include <assetlib/texture_prune.h>
#include <bgl/IGraphics.h>
#include <core/file/file.h>
#include <core/settings/Settings.h>

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

	{
		const auto     configPath = core::file::getLibraryPath().parent_path() / "config.json";
		core::Settings settings(configPath);
		const auto     gfxSettings = settings["graphics"];

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

		m_Graphics = bgl::CreateGraphics(gfxOpts);

		// The editor's one Scene. Every viewport (the Level Editor, the Material Editor's model
		// preview) renders it through a SceneView of its own, so geometry, textures and materials
		// are pooled here once and these budgets must cover all of them together.
		auto sceneDesc         = bgl::SceneDesc();
		auto sceneSettings     = settings["scene"];
		sceneDesc.maxGeom      = sceneSettings["maxGeom"].GetOrDefault(256);
		sceneDesc.maxMeshlets  = sceneSettings["maxMeshlets"].GetOrDefault(32768);
		sceneDesc.maxSubmeshes = sceneSettings["maxSubmeshes"].GetOrDefault(512);
		sceneDesc.maxVertexBufferByteSize =
			sceneSettings["maxVertexBufferByteSize"].GetOrDefault(33554432);
		sceneDesc.maxIndices           = sceneSettings["maxIndices"].GetOrDefault(2000000);
		sceneDesc.maxPbrMaterials      = sceneSettings["maxPbrMaterials"].GetOrDefault(256);
		sceneDesc.maxLoosePbrMaterials = sceneSettings["maxLoosePbrMaterials"].GetOrDefault(256);

		m_Scene = m_Graphics->CreateScene(sceneDesc);

		auto levelDesc         = RenderTargetWindowDesc();
		levelDesc.gfx          = m_Graphics;
		levelDesc.scene        = m_Scene;
		levelDesc.maxInstances = settings["levelEditor"]["maxInstances"].GetOrDefault(1000);

		m_LevelEditor = new LevelEditorWindow(this, std::move(levelDesc));

		auto matSettings              = settings["materialEditor"];
		auto matDesc                  = MaterialEditorWindowDesc();
		matDesc.gfx                   = m_Graphics;
		matDesc.scene                 = m_Scene;
		matDesc.maxPreviewInstances   = matSettings["maxPreviewInstances"].GetOrDefault(16u);
		matDesc.previewEnv.skybox     = matSettings["skybox"].GetOrDefault(std::string());
		matDesc.previewEnv.irradiance = matSettings["irradiance"].GetOrDefault(std::string());
		matDesc.previewEnv.prefilter  = matSettings["prefilter"].GetOrDefault(std::string());
		matDesc.previewEnv.brdfLut    = matSettings["brdfLut"].GetOrDefault(std::string());
		matDesc.previewEnv.exposure   = matSettings["exposure"].GetOrDefault(1.0f);

		m_MaterialEditor = new MaterialEditorWindow(this, std::move(matDesc));
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
	m_ContentExplorer = new ContentExplorerWindow(m_ContentExplorerDock);

	m_ContentExplorer->setMinimumSize(0, 0);
	m_ContentExplorerDock->setWidget(m_ContentExplorer);
	addDockWidget(Qt::BottomDockWidgetArea, m_ContentExplorerDock);

	m_Ui.menuWindow->addAction(m_LevelEditorDock->toggleViewAction());
	m_Ui.menuWindow->addAction(m_MaterialEditorDock->toggleViewAction());
	m_Ui.menuWindow->addAction(m_ContentExplorerDock->toggleViewAction());

	ShowEmptyState();
}

MainWindow::~MainWindow() = default;

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

	try
	{
		SetActiveProject(Project::Open(std::filesystem::path(file.toStdWString())));
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(this, "Open Project", e.what());
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
