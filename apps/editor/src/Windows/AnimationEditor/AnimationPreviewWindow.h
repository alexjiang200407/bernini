#pragma once

#include "Render/OrbitCamera.h"
#include "Render/environment.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "util/held_open_assets.h"

#include <bgl/GeomHandle.h>
#include <bgl/InstanceDesc.h>
#include <bgl/MaterialHandle.h>
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
class QHideEvent;
class QMouseEvent;
class QShowEvent;
class QWheelEvent;

/**
 * The Animation panel's viewport: a dropped or opened rigged `.bmesh` shown wearing its own
 * materials against the configured environment, under an orbit camera. A rigged mesh with clips
 * plays through whichever pose source is selected -- posed per instance by default, or read off the
 * rig's shared table -- with its static entries beside it, and one with no clips stands in its bind
 * pose. A mesh with no rig at all is refused: nothing to animate.
 *
 * Everything is acquired through `game::AssetManager`, so a mesh renders here exactly as it does
 * anywhere else the manager serves; SetAssets(nullptr) releases everything held, and MainWindow
 * calls it before the manager itself is torn down.
 *
 * The window has no clock of its own: the panel owns the transport and feeds SetTime.
 */
class AnimationPreviewWindow : public RenderTargetWindow, public editor::IHoldsAssets
{
	Q_OBJECT

public:
	AnimationPreviewWindow(
		QWidget*                     parent,
		RenderTargetWindowDesc       rt,
		editor::EnvironmentApplyDesc env);
	~AnimationPreviewWindow() override;

	/** The `.benv` this view is lit by, which must not be deleted while it is still drawing it. */
	[[nodiscard]] QStringList
	GetHeldOpenPaths() const override;

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

	/** Respawns the animated instances on clip `index`; the caller's transport is the clock. */
	void
	SetActiveClip(uint32_t index);

	/**
	 * Where the preview's instances read their pose. Switching respawns them on the same upload --
	 * both sources draw one geom, which is the property the crowd tier was built for.
	 *
	 * The two draw the same pixels at a whole frame, so this is not a difference to look for on
	 * screen: it is how the crowd path gets exercised at all outside a test.
	 *
	 * A no-op if `source` is already the active one.
	 */
	void
	SetPoseSource(bgl::PoseSource source);

	[[nodiscard]] bgl::PoseSource
	GetPoseSource() const noexcept
	{
		return m_Source;
	}

	/**
	 * Tilts the ground the rig stands on: the scene's ground plane, which a planted foot is solved
	 * against, and the floor drawn under the rig so the tilt can be seen. Positive rises toward +X.
	 *
	 * A rebind, not a per-frame input: setting the ground moves the scene's temporal epoch, so a
	 * control driving this should commit on release rather than on every tick of a drag -- a drag
	 * that committed each tick would keep the preview unaccumulated for the whole gesture.
	 *
	 * The scene is shared with every other viewport, and its ground with it, so the slope is
	 * applied only while this window is shown and the ground goes flat when it is hidden. The
	 * value is kept either way, and comes back with the window.
	 */
	void
	SetGroundSlope(float degrees);

	[[nodiscard]] float
	GetGroundSlope() const noexcept
	{
		return m_SlopeDegrees;
	}

	/** Back to the empty state: geometry released, environment kept, ground left flat. */
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

	/**
	 * The pose source the panel is now on, emitted after every SetPoseSource. The caller's control
	 * has already moved by then, so this is what a selector reads to agree with the panel rather
	 * than with the click.
	 */
	void
	PoseSourceChanged(bgl::PoseSource source);

	/** The clip table now playable (empty: bind pose only). Feed it to the transport. */
	void
	ClipsChanged(const std::vector<editor::ClipInfo>& clips);

protected:
	void
	resizeEvent(QResizeEvent* event) override;
	void
	showEvent(QShowEvent* event) override;
	void
	hideEvent(QHideEvent* event) override;

	void
	dragEnterEvent(QDragEnterEvent* event) override;
	void
	dragMoveEvent(QDragMoveEvent* event) override;
	void
	dropEvent(QDropEvent* event) override;

	// Lights the preview from a dropped `.benv`, resolved against the open project's data root.
	void
	SetEnvironment(const std::string& benvPath);

	// Puts the environment config.json named back, if a drop displaced it. Part of Clear, because a
	// preview that has given up its mesh but kept the backdrop somebody dropped on it is showing
	// neither what it was configured with nor what it was asked to show.
	void
	RestoreConfiguredEnvironment();

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
		const QString&               refusal,
		std::span<const uint32_t>    refusedEntries);

	void
	UpdateCamera();

	// Releases the instances, then the geoms they reference, through the manager.
	void
	ClearGeometry();

	/** Places one animated instance on `clip` at phase 0, rate 1, reading the active pose source. */
	[[nodiscard]] bgl::MeshInstanceHandle
	SpawnAnimated(bgl::GeomHandle geom, const glm::mat4& world, uint32_t clip);

	/**
	 * Sets the scene's ground to the current slope and stands the floor under the rig at the same
	 * tilt. Render thread only. The floor is a placement, and a placement does not move: it is
	 * deleted and re-placed, which is what a slope change costs.
	 */
	void
	PlaceGround();

	// One animated placement's live state: respawned in place on a clip switch, and on a tier
	// switch, which is the same destroy-and-recreate against the same geom.
	struct AnimatedDraw
	{
		bgl::GeomHandle         geom;
		glm::mat4               world = glm::mat4(1.0f);
		bgl::MeshInstanceHandle instance;
	};

	game::AssetManager* m_Assets     = nullptr;
	uint32_t            m_ActiveClip = 0;  // what the live animated instances were spawned on
	bgl::PoseSource     m_Source     = bgl::PoseSource::kPerInstance;

	std::vector<bgl::MeshInstanceHandle> m_Instances;  // static entries
	std::vector<bgl::GeomHandle>         m_Geoms;      // one entry per acquire, repeats included
	std::vector<AnimatedDraw>            m_AnimatedDraws;
	std::filesystem::path                m_DataRoot;

	// The floor: one plane geom for the window's life, in the shared scene like the material
	// preview's sphere, and a placement in this view alone while a rig is shown.
	bgl::MaterialHandle     m_GroundMaterial;
	bgl::GeomHandle         m_GroundGeom;
	bgl::MeshInstanceHandle m_GroundInstance;
	float                   m_SlopeDegrees = 0.0f;

	// The configured environment is kept whole because a drop carries only a path and Clear has to
	// be able to get back to it. Its root stands in until a project opens and m_DataRoot names its
	// own.
	editor::EnvironmentBinding m_Environment;

	editor::OrbitCamera m_Orbit;

	QPoint          m_LastMousePos;
	Qt::MouseButton m_DragButton = Qt::NoButton;
};
