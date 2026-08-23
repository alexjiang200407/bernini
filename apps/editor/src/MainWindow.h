#pragma once

#include <QMainWindow>

#include <assetlib/Project.h>
#include <gamelib/AssetManager.h>

#include "ui_MainWindow.h"

class QDockWidget;
class QLabel;
class QMenu;
class ContentExplorerWindow;
class AssetThumbnailCache;
class AnimationEditorWindow;
class LevelEditorWindow;
class MaterialEditorWindow;
class RenderTargetWindow;
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

	/**
	 * Offers to re-extract the textures of every source changed since it was imported, and does
	 * it. On project open, because that is when a re-export first reaches the editor. An offer
	 * rather than a step: it costs an import's worth of supercompression.
	 */
	void
	OfferTextureRefresh();

	void
	SetActiveProject(assetlib::Project project);

	void
	ShowEmptyState();

	void
	ShowProjectState();

	// Keeps every RenderTargetWindow under `dock` in the frame loop only while the dock is the
	// selected tab.
	void
	DriveViewportsFromTab(QDockWidget* dock);

	// Adds the viewport frame-time readout to the status bar and connects every viewport to it. The
	// three viewport docks are tabbed together, so at most one of them reports at a time.
	void
	SetUpFrameStats();

	/** Everything the constructor does once its base is built, so a failure can be caught around it. */
	void
	Build();

	/**
	 * Hands back everything that renders, in the order it has to go: the thumbnails and the assets
	 * release through the Renderer, and the viewports outlive both.
	 *
	 * Called by the destructor, and by the constructor when it fails part-way. Qt destroys the
	 * viewports as children of this window, which happens *after* its members -- so leaving it to
	 * either destructor alone puts ~RenderTargetWindow on the far side of ~m_Renderer. Safe with any
	 * of it not built yet.
	 */
	void
	ReleaseRenderResources() noexcept;

	// The Render menu. Its entries toggle temporal AA and set the viewports' render scale, which is
	// how a temporal artifact gets judged -- the difference is what shows it, and a restart loses that.
	void
	SetUpRenderMenu();

	void
	SetUpRenderScaleMenu(QMenu* render);

	// The reconstruction-width entries, which sit under the scale ones because they only mean
	// anything at a scale below 1.
	void
	SetUpReconstructionWidthMenu(QMenu* render);

	Ui::MainWindow m_Ui;

	QString m_InstanceName;

	std::unique_ptr<assetlib::Project> m_Project;
	ContentExplorerWindow*             m_ContentExplorer     = nullptr;
	LevelEditorWindow*                 m_LevelEditor         = nullptr;
	MaterialEditorWindow*              m_MaterialEditor      = nullptr;
	AnimationEditorWindow*             m_AnimationEditor     = nullptr;
	QDockWidget*                       m_LevelEditorDock     = nullptr;
	QDockWidget*                       m_MaterialEditorDock  = nullptr;
	QDockWidget*                       m_AnimationEditorDock = nullptr;
	QDockWidget*                       m_ContentExplorerDock = nullptr;
	QLabel*                            m_FrameStats          = nullptr;

	// The viewport the readout is currently about, or null when none is visible. A viewport that
	// leaves the frame loop stops reporting, so without this its last figures would stay on the
	// status bar and be read as the visible viewport's.
	RenderTargetWindow* m_FrameStatsSource = nullptr;

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
