#include "MainWindow.h"

#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>

#include "Project/Project.h"
#include "Windows/ContentExplorer/ContentExplorerWindow.h"
#include "Windows/LevelEditor/LevelEditorWindow.h"
#include "Windows/MaterialEditor/MaterialEditorWindow.h"
#include <bgl/IGraphics.h>
#include <core/file/file.h>
#include <core/settings/Settings.h>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
	m_Ui.setupUi(this);

	connect(m_Ui.actionNewProject, &QAction::triggered, this, &MainWindow::NewProject);
	connect(m_Ui.actionOpenProject, &QAction::triggered, this, &MainWindow::OpenProject);
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

		// The scene rendered in the default Level Editor viewport.
		auto sceneDesc         = bgl::SceneDesc();
		auto sceneSettings     = settings["scene"];
		sceneDesc.maxGeom      = sceneSettings["maxGeom"].GetOrDefault(100);
		sceneDesc.maxMeshlets  = sceneSettings["maxMeshlets"].GetOrDefault(1000);
		sceneDesc.maxSubmeshes = sceneSettings["maxSubmeshes"].GetOrDefault(100);
		sceneDesc.maxVertexBufferByteSize =
			sceneSettings["maxVertexBufferByteSize"].GetOrDefault(400000);
		sceneDesc.maxIndices      = sceneSettings["maxIndices"].GetOrDefault(100000);
		sceneDesc.maxPbrMaterials = sceneSettings["maxPbrMaterials"].GetOrDefault(200);

		m_Scene = m_Graphics->CreateScene(sceneDesc);

		auto levelDesc         = RenderTargetWindowDesc();
		levelDesc.gfx          = m_Graphics;
		levelDesc.scene        = m_Scene;
		levelDesc.maxInstances = settings["levelEditor"]["maxInstances"].GetOrDefault(1000);

		m_LevelEditor = new LevelEditorWindow(this, std::move(levelDesc));

		auto matSettings              = settings["materialEditor"];
		auto matDesc                  = MaterialEditorWindowDesc();
		matDesc.gfx                   = m_Graphics;
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
	m_Ui.menuEdit->setEnabled(true);
	m_Ui.menuWindow->setEnabled(true);

	resizeDocks({ m_LevelEditorDock, m_ContentExplorerDock }, { 700, 220 }, Qt::Vertical);
}
