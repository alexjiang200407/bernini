#pragma once

#include "Render/OrbitCamera.h"
#include "Render/environment.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"
#include "Windows/AnimationEditor/animation_source.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "util/held_open_assets.h"

#include <bgl/GeomHandle.h>
#include <bgl/MeshInstanceHandle.h>
#include <gamelib/vat_freshness.h>

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
 * plays through whichever tier is selected -- skinned by default, or VAT -- with its static entries
 * beside it, and one with no clips stands in its bind pose. A mesh with no rig at all is refused:
 * nothing to animate.
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
	 * Which tier the preview animates through. Switching re-loads the shown mesh, because the two
	 * are different uploads -- and that is the point of having the switch: the skinned pose is what
	 * the rig says, the VAT one is what the bake made of it, and the difference between them is a
	 * bake bug you can otherwise only find in the game.
	 *
	 * A no-op if nothing is shown, or if `source` is already the active one.
	 */
	void
	SetAnimationSource(editor::AnimationSource source);

	[[nodiscard]] editor::AnimationSource
	GetAnimationSource() const noexcept
	{
		return m_Source;
	}

	/** Back to the empty state: geometry released, environment kept. */
	void
	Clear();

	/**
	 * Bakes the shown mesh's `.bvat` for the clip set it is playing, under a loading screen, and
	 * reloads if VAT is what is on screen. A bake is seconds of CPU skinning, which is why nothing
	 * does it implicitly -- see AnimationLoadSteps::needsFreshBake.
	 *
	 * Asks first, every press, naming the size the bake would write: the pair is hundreds of
	 * megabytes on a dense rig, and this is a button one click away from Close.
	 *
	 * Does nothing when no mesh is shown or it has no clips: there is no pair to bake.
	 */
	void
	BakeShownVat();

	/** Whether BakeShownVat has something to bake -- what the Bake VAT button's enabled state is. */
	[[nodiscard]] bool
	CanBakeVat() const noexcept
	{
		return !m_MeshPath.empty() && !m_Animations.empty();
	}

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
	 * The tier the panel is *actually* previewing through. Emitted after every SetAnimationSource,
	 * including one whose load failed -- the caller's control has already moved by then, and this is
	 * what snaps it back to what is on screen.
	 */
	void
	PreviewSourceChanged(editor::AnimationSource source);

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
	 * The "not baked / out of date" dialog with its Bake Now button, shown when the VAT tier is
	 * asked for and no usable bake exists. It names what the bake would write. Taking the offer
	 * bakes and re-enters the load; declining leaves the panel showing what it already had, the
	 * tier included.
	 */
	void
	OfferBakeForTier(
		const std::filesystem::path& absolutePath,
		const std::string&           animations,
		const QString&               name,
		game::VatBakeState           state);

	/** The bake itself, off the UI thread. False when it was cancelled or failed (it reports). */
	[[nodiscard]] bool
	BakeVat(const std::filesystem::path& absolutePath, const std::string& animations);

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

	/**
	 * Places one animated instance on `clip` at phase 0, rate 1.
	 *
	 * `source` is explicit rather than read from m_Source: during a tier switch the geom has been
	 * acquired through the *candidate* tier while m_Source still names the old one, and creating an
	 * instance of the wrong kind for a geom is a SceneError.
	 */
	[[nodiscard]] bgl::MeshInstanceHandle
	SpawnAnimated(
		bgl::GeomHandle         geom,
		const glm::mat4&        world,
		uint32_t                clip,
		editor::AnimationSource source);

	/**
	 * LoadMesh's body, carrying the tier to load *through* rather than reading m_Source. m_Source is
	 * assigned only once the load has stood something up, so a failed switch leaves the panel
	 * describing what is still on screen.
	 */
	void
	LoadMeshAs(
		const std::filesystem::path& absolutePath,
		const std::string&           animationsRelPath,
		editor::AnimationSource      source);

	// One animated placement's live state, whichever tier it was acquired through: respawned in
	// place on a clip switch.
	struct AnimatedDraw
	{
		bgl::GeomHandle         geom;
		glm::mat4               world = glm::mat4(1.0f);
		bgl::MeshInstanceHandle instance;
	};

	game::AssetManager*     m_Assets     = nullptr;
	uint32_t                m_ActiveClip = 0;  // what the live animated instances were spawned on
	editor::AnimationSource m_Source     = editor::AnimationSource::kSkinned;

	std::vector<bgl::MeshInstanceHandle> m_Instances;  // static entries
	std::vector<bgl::GeomHandle>         m_Geoms;      // one entry per acquire, repeats included
	std::vector<AnimatedDraw>            m_AnimatedDraws;
	std::filesystem::path                m_DataRoot;

	// What LoadMesh was last called with, so a source switch can re-load without the caller
	// re-supplying them.
	std::filesystem::path m_MeshPath;
	std::string           m_Animations;

	// The configured environment is kept whole because a drop carries only a path and Clear has to
	// be able to get back to it. Its root stands in until a project opens and m_DataRoot names its
	// own.
	editor::EnvironmentBinding m_Environment;

	editor::OrbitCamera m_Orbit;

	QPoint          m_LastMousePos;
	Qt::MouseButton m_DragButton = Qt::NoButton;
};
