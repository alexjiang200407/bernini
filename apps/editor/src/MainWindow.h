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
class Renderer;
class VersionControlActions;

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
	SetActiveProject(assetlib::Project project);

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

	// The Version Control menu. Disabled, rather than hidden, when the project is not inside a
	// repository -- so the answer to "where is version control" is where the question is asked.
	void
	SetUpVersionControlMenu();

	void
	SetVersionControlAvailable(bool available);

	/**
	 * Every asset a panel is holding open, absolute.
	 *
	 * Asked afresh at each use rather than kept, so no copy of the answer can go stale.
	 */
	[[nodiscard]] QStringList
	GetHeldOpenPaths() const;

	Ui::MainWindow m_Ui;

	QString m_InstanceName;

	std::unique_ptr<assetlib::Project> m_Project;
	VersionControlActions*             m_VersionControl      = nullptr;
	QMenu*                             m_VersionControlMenu  = nullptr;
	ContentExplorerWindow*             m_ContentExplorer     = nullptr;
	LevelEditorWindow*                 m_LevelEditor         = nullptr;
	MaterialEditorWindow*              m_MaterialEditor      = nullptr;
	AnimationEditorWindow*             m_AnimationEditor     = nullptr;
	QDockWidget*                       m_LevelEditorDock     = nullptr;
	QDockWidget*                       m_MaterialEditorDock  = nullptr;
	QDockWidget*                       m_AnimationEditorDock = nullptr;
	QDockWidget*                       m_ContentExplorerDock = nullptr;
	QLabel*                            m_FrameStats          = nullptr;

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
