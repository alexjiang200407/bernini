#pragma once

#include "Render/OrbitCamera.h"
#include "Render/environment.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"

#include <bgl/GeomHandle.h>
#include <bgl/MeshInstanceHandle.h>

namespace assetlib
{
	struct BMesh;
}

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
 * The Animation panel's viewport: a dropped or opened rigged `.bmesh` shown wearing its own
 * materials against the configured environment, under an orbit camera. A rigged mesh with clips
 * plays as VAT -- its skinned entries acquired through AcquireVatMesh, its static entries beside
 * them -- and one with none stands in its bind pose. A mesh with no rig at all is refused:
 * nothing to animate.
 *
 * Everything is acquired through `game::AssetManager`, so a mesh renders here exactly as it does
 * anywhere else the manager serves; SetAssets(nullptr) releases everything held, and MainWindow
 * calls it before the manager itself is torn down.
 *
 * The window has no clock of its own: the panel owns the transport and feeds SetTime.
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
	// TODO: feat/archive mounts will stand behind these paths; the drop containment check and the
	// drawsLoose probe both assume a loose filesystem today.
	void
	SetDataRoot(const std::filesystem::path& dataRoot)
	{
		m_DataRoot = dataRoot;
	}

	/**
	 * Replaces the preview with the mesh at `absolutePath` (which must live under the data root),
	 * played from `animationsRelPath` -- or from the first resolved candidate when empty. A rig
	 * whose clips are stale re-bakes under the loading screen before anything is uploaded.
	 *
	 * What ends up shown is announced by the signals below; a failure warns and clears.
	 */
	void
	LoadMesh(const std::filesystem::path& absolutePath, const std::string& animationsRelPath = {});

	/** Respawns the VAT instances on clip `index`; the caller's transport is the clock. */
	void
	SetActiveClip(uint32_t index);

	/** Back to the empty state: geometry released, environment kept. */
	void
	Clear();

Q_SIGNALS:
	/** The preview now shows the mesh at this data-root-relative path (empty: cleared). */
	void
	MeshChanged(const QString& relPath);

	/**
	 * Bake Now rewrote `relPath` on disk. Anything showing what that file says -- the Material
	 * Editor's properties panel -- has to re-read it; MainWindow routes this the way it routes the
	 * Content Explorer's bakes.
	 */
	void
	MaterialBaked(const QString& relPath);

	/** The `.banim` candidates for the shown mesh, and which one is playing (-1: none). */
	void
	AnimationSourcesChanged(const QStringList& candidates, int activeIndex);

	/** The clip table now playable (empty: bind pose only). Feed it to the transport. */
	void
	ClipsChanged(const std::vector<editor::ClipInfo>& clips);

protected:
	void
	resizeEvent(QResizeEvent* event) override;

	void
	dragEnterEvent(QDragEnterEvent* event) override;
	void
	dragMoveEvent(QDragMoveEvent* event) override;
	void
	dropEvent(QDropEvent* event) override;

	/**
	 * Lights the preview from `benvPath`, releasing whatever the last one bound.
	 *
	 * Without the release each dropped environment would keep its predecessor's three cube maps
	 * uploaded for the life of the window, and the scene's texture slots are bounded.
	 */
	void
	SetEnvironment(const std::string& benvPath);

	// Puts the environment config.json named back, if a drop displaced it. Part of Clear, because a
	// preview that has given up its mesh but kept the backdrop somebody dropped on it is showing
	// neither what it was configured with nor what it was asked to show.
	void
	RestoreConfiguredEnvironment();

	void
	ApplyEnvironmentFrom(const std::string& benvPath, const std::filesystem::path& dataRoot);

	void
	mousePressEvent(QMouseEvent* event) override;
	void
	mouseMoveEvent(QMouseEvent* event) override;
	void
	mouseReleaseEvent(QMouseEvent* event) override;
	void
	wheelEvent(QWheelEvent* event) override;

private:
	/**
	 * The refusal dialog, with a Bake Now button when the cause is fixable here: materials the
	 * mesh names that are routed but never composited. Baking runs like the Content Explorer's --
	 * off the UI thread, cancellable -- and a completed bake reloads the mesh.
	 */
	void
	OfferBakeForRefusal(
		const assetlib::BMesh&       mesh,
		const std::filesystem::path& absolutePath,
		const std::string&           animations,
		const QString&               name,
		const QString&               refusal);

	void
	UpdateCamera();

	// Releases the instances, then the geoms they reference, through the manager.
	void
	ClearGeometry();

	// One VAT placement's live state: respawned in place on a clip switch.
	struct VatDraw
	{
		bgl::GeomHandle         geom;
		glm::mat4               world = glm::mat4(1.0f);
		bgl::MeshInstanceHandle instance;
	};

	game::AssetManager* m_Assets     = nullptr;
	uint32_t            m_ActiveClip = 0;  // what the live VAT instances were spawned on

	std::vector<bgl::MeshInstanceHandle> m_Instances;  // static entries
	std::vector<bgl::GeomHandle>         m_Geoms;      // one entry per acquire, repeats included
	std::vector<VatDraw>                 m_VatDraws;
	std::filesystem::path                m_DataRoot;

	// What the last ApplyEnvironment bound, so the next one can release it.
	editor::AppliedEnvironment m_Environment;

	// What the window was configured with, kept whole because a drop carries only a path and Clear
	// has to be able to get back to this. The configured root stands in until a project opens and
	// m_DataRoot names its own.
	editor::EnvironmentApplyDesc m_Configured;

	// The `.benv` currently bound, so Clear can tell a drop from the configured one it already has.
	std::string m_AppliedEnv;

	editor::OrbitCamera m_Orbit;

	QPoint          m_LastMousePos;
	Qt::MouseButton m_DragButton = Qt::NoButton;
};
