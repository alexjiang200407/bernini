#include "MainWindow.h"

#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
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
		auto sceneDesc        = bgl::SceneDesc();
		auto sceneSettings    = settings["scene"];
		sceneDesc.maxGeom     = sceneSettings["maxGeom"].GetOrDefault(100);
		sceneDesc.maxMeshlets = sceneSettings["maxMeshlets"].GetOrDefault(1000);
		sceneDesc.maxVertices = sceneSettings["maxVertices"].GetOrDefault(100000);
		sceneDesc.maxIndices  = sceneSettings["maxIndices"].GetOrDefault(100000);

		m_Scene = m_Graphics->CreateScene(sceneDesc);

		auto levelDesc         = RenderTargetWindowDesc();
		levelDesc.gfx          = m_Graphics;
		levelDesc.scene        = m_Scene;
		levelDesc.maxInstances = settings["levelEditor"]["maxInstances"].GetOrDefault(1000);

		m_LevelEditor = new LevelEditorWindow(this, std::move(levelDesc));
	}

	m_MaterialEditor = new MaterialEditorWindow(this);

	setCentralWidget(nullptr);
	setDockNestingEnabled(true);
	setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

	m_LevelEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_LevelEditor->setMinimumSize(256, 256);

	auto* levelEditorDock = new QDockWidget("Level Editor", this);
	levelEditorDock->setObjectName("LevelEditorDock");
	levelEditorDock->setWidget(m_LevelEditor);
	levelEditorDock->setTitleBarWidget(new QWidget(levelEditorDock));
	addDockWidget(Qt::TopDockWidgetArea, levelEditorDock);

	auto* materialEditorDock = new QDockWidget("Material Editor", this);
	materialEditorDock->setObjectName("MaterialEditorDock");
	materialEditorDock->setWidget(m_MaterialEditor);
	materialEditorDock->setTitleBarWidget(new QWidget(materialEditorDock));
	addDockWidget(Qt::TopDockWidgetArea, materialEditorDock);

	tabifyDockWidget(levelEditorDock, materialEditorDock);
	levelEditorDock->raise();

	auto* contentExplorerDock = new QDockWidget("Content Explorer", this);
	contentExplorerDock->setObjectName("ContentExplorerDock");
	m_ContentExplorer = new ContentExplorerWindow(contentExplorerDock);

	m_ContentExplorer->setMinimumSize(0, 0);
	contentExplorerDock->setWidget(m_ContentExplorer);
	addDockWidget(Qt::BottomDockWidgetArea, contentExplorerDock);

	m_Ui.menuWindow->addAction(levelEditorDock->toggleViewAction());
	m_Ui.menuWindow->addAction(materialEditorDock->toggleViewAction());
	m_Ui.menuWindow->addAction(contentExplorerDock->toggleViewAction());

	resizeDocks({ levelEditorDock, contentExplorerDock }, { 700, 220 }, Qt::Vertical);
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

	m_ContentExplorer->SetRootPath(
		QString::fromStdWString(m_Project->GetDataDirectory().wstring()));

	setWindowTitle(
		QString("Bernini Editor — %1").arg(QString::fromStdString(m_Project->GetName())));
	statusBar()->showMessage(
		QString("Project data: %1")
			.arg(QString::fromStdString(m_Project->GetDataDirectory().string())));
}
