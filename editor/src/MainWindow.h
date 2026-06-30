#pragma once

#include <QMainWindow>

#include "ui_MainWindow.h"

class Project;
class ContentExplorerWindow;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow();

private:
	void
	NewProject();

	void
	OpenProject();

	void
	SetActiveProject(Project project);

	Ui::MainWindow           m_Ui;
	std::unique_ptr<Project> m_Project;
	ContentExplorerWindow*   m_ContentExplorer = nullptr;
};
