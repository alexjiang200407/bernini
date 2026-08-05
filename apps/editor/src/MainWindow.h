#pragma once

#include <QMainWindow>

#include <gamelib/AssetManager.h>

#include "ui_MainWindow.h"

class QDockWidget;
class QLabel;
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

protected:
	// Stops the viewports and drains the GPU while every window is still on screen. A present left
	// pending when its window hides is never consumed, and it wedges every later fence wait on the
	// queue -- the shutdown flushes among them.
	void
	closeEvent(QCloseEvent* event) override;

private:
	void
	NewProject();

	void
	OpenProject();

	// Opens `path`, reporting a failure to the user rather than throwing. False when it could not
	// be opened, so a caller can fall back.
	bool
	OpenProjectAt(const std::filesystem::path& path);

	void
	CleanUnusedTextures();

	void
	SetActiveProject(Project project);

	void
	ShowEmptyState();

	void
	ShowProjectState();

	// Keeps every RenderTargetWindow under `dock` in the frame loop only while the dock is the
	// selected tab.
	void
	DriveViewportsFromTab(QDockWidget* dock);

	// Adds the viewport frame-time readout to the status bar and connects it to the level editor.
	void
	SetUpFrameStats();

	Ui::MainWindow            m_Ui;
	std::unique_ptr<Project>  m_Project;
	ContentExplorerWindow*    m_ContentExplorer     = nullptr;
	LevelEditorWindow*        m_LevelEditor         = nullptr;
	MaterialEditorWindow*     m_MaterialEditor      = nullptr;
	QDockWidget*              m_LevelEditorDock     = nullptr;
	QDockWidget*              m_MaterialEditorDock  = nullptr;
	QDockWidget*              m_ContentExplorerDock = nullptr;
	QLabel*                   m_FrameStats          = nullptr;
	std::unique_ptr<Renderer> m_Renderer;

	// The dock-visibility connections, held so the destructor can cut them before the windows they
	// reach go away.
	std::vector<QMetaObject::Connection> m_TabVisibility;

	// Rebuilt per project: it resolves every path against that project's Data root.
	std::unique_ptr<game::AssetManager> m_Assets;

	// Declared last, so it is destroyed first: its destructor releases geometry and materials through
	// the Renderer and the AssetManager, both of which must outlive it.
	std::unique_ptr<AssetThumbnailCache> m_Thumbnails;
};
