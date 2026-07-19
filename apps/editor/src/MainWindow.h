#pragma once

#include <QMainWindow>

#include <gamelib/AssetManager.h>

#include "ui_MainWindow.h"

class QDockWidget;
class Project;
class ContentExplorerWindow;
class AssetThumbnailCache;
class LevelEditorWindow;
class MaterialEditorWindow;
class Renderer;

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
	CleanUnusedTextures();

	void
	SetActiveProject(Project project);

	void
	ShowEmptyState();

	void
	ShowProjectState();

	Ui::MainWindow            m_Ui;
	std::unique_ptr<Project>  m_Project;
	ContentExplorerWindow*    m_ContentExplorer     = nullptr;
	LevelEditorWindow*        m_LevelEditor         = nullptr;
	MaterialEditorWindow*     m_MaterialEditor      = nullptr;
	QDockWidget*              m_LevelEditorDock     = nullptr;
	QDockWidget*              m_MaterialEditorDock  = nullptr;
	QDockWidget*              m_ContentExplorerDock = nullptr;
	std::unique_ptr<Renderer> m_Renderer;

	// The editor's one asset manager, over the Level Editor's view. Shared, so a material loaded by
	// the thumbnails and by the level is one upload and one reference count. Rebuilt per project,
	// because it resolves every path against that project's Data root.
	std::unique_ptr<game::AssetManager> m_Assets;

	// Declared last, so it is destroyed first: its destructor releases geometry and materials through
	// the Renderer and the AssetManager, both of which must outlive it.
	std::unique_ptr<AssetThumbnailCache> m_Thumbnails;
};
