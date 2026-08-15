#pragma once

#include "Render/OrbitCamera.h"
#include "Render/environment.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"

#include <bgl/GeomHandle.h>
#include <bgl/MeshInstanceHandle.h>

namespace game
{
	class AssetManager;
}

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMouseEvent;
class QWheelEvent;

/**
 * The Animation panel's viewport: a dropped or opened `.bmesh` shown wearing its own materials
 * against the configured environment, under an orbit camera. A rigged mesh with no clips to play
 * stands in its bind pose.
 *
 * Geometry and materials are acquired through `game::AssetManager`, so a mesh renders here exactly
 * as it does anywhere else the manager serves; SetAssets(nullptr) releases everything held, and
 * MainWindow calls it before the manager itself is torn down.
 */
class AnimationPreviewWindow : public RenderTargetWindow
{
	Q_OBJECT

public:
	AnimationPreviewWindow(
		QWidget*                     parent,
		RenderTargetWindowDesc       rt,
		editor::EnvironmentApplyDesc env);
	~AnimationPreviewWindow() override;

	/**
	 * The manager this preview acquires through, or nullptr to release everything held. The
	 * manager must outlive every acquisition, so the owner clears this before destroying it.
	 */
	void
	SetAssets(game::AssetManager* assets);

	// The project's Data directory: what a dropped absolute path is resolved against, and the
	// root the manager's relative paths mean.
	void
	SetDataRoot(const std::filesystem::path& dataRoot)
	{
		m_DataRoot = dataRoot;
	}

	/** Replaces the preview with the mesh at `absolutePath`; must live under the data root. */
	void
	LoadMesh(const std::filesystem::path& absolutePath);

	/** Back to the empty state: geometry released, environment kept. */
	void
	Clear();

Q_SIGNALS:
	/** The preview now shows the mesh at this data-root-relative path (empty: cleared). */
	void
	MeshChanged(const QString& relPath);

protected:
	void
	resizeEvent(QResizeEvent* event) override;

	void
	dragEnterEvent(QDragEnterEvent* event) override;
	void
	dragMoveEvent(QDragMoveEvent* event) override;
	void
	dropEvent(QDropEvent* event) override;

	void
	mousePressEvent(QMouseEvent* event) override;
	void
	mouseMoveEvent(QMouseEvent* event) override;
	void
	mouseReleaseEvent(QMouseEvent* event) override;
	void
	wheelEvent(QWheelEvent* event) override;

private:
	void
	UpdateCamera();

	// Releases the instances, then the geoms they reference, through the manager.
	void
	ClearGeometry();

	game::AssetManager* m_Assets = nullptr;

	std::vector<bgl::MeshInstanceHandle> m_Instances;
	std::vector<bgl::GeomHandle>         m_Geoms;  // one entry per acquire, repeats included
	std::filesystem::path                m_DataRoot;

	// What the constructor's ApplyEnvironment bound; released only by scene teardown today.
	editor::AppliedEnvironment m_Environment;

	editor::OrbitCamera m_Orbit;

	QPoint          m_LastMousePos;
	Qt::MouseButton m_DragButton = Qt::NoButton;
};
