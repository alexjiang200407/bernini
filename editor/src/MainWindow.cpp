#include "MainWindow.h"

#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QStatusBar>

#include "Project/Project.h"
#include "Windows/ContentExplorer/ContentExplorerWindow.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
	m_Ui.setupUi(this);

	connect(m_Ui.actionNewProject, &QAction::triggered, this, &MainWindow::NewProject);
	connect(m_Ui.actionOpenProject, &QAction::triggered, this, &MainWindow::OpenProject);
	connect(m_Ui.actionExit, &QAction::triggered, this, &QWidget::close);

	auto* contentExplorerDock = new QDockWidget("Content Explorer", this);
	contentExplorerDock->setObjectName("ContentExplorerDock");
	m_ContentExplorer = new ContentExplorerWindow(contentExplorerDock);
	contentExplorerDock->setWidget(m_ContentExplorer);
	addDockWidget(Qt::BottomDockWidgetArea, contentExplorerDock);

	m_Ui.menuWindow->addAction(contentExplorerDock->toggleViewAction());
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
