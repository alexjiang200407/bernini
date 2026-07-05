#pragma once

#include <QMainWindow>

#include <core/ref/SharedRef.h>

#include "ui_MainWindow.h"

class Project;
class ContentExplorerWindow;
class LevelEditorWindow;
class MaterialEditorWindow;

namespace bgl
{
	class IScene;
	class IGraphics;
}

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

	Ui::MainWindow                  m_Ui;
	std::unique_ptr<Project>        m_Project;
	ContentExplorerWindow*          m_ContentExplorer = nullptr;
	LevelEditorWindow*              m_LevelEditor     = nullptr;
	MaterialEditorWindow*           m_MaterialEditor  = nullptr;
	core::SharedRef<bgl::IGraphics> m_Graphics;
	core::SharedRef<bgl::IScene>    m_Scene;
};
