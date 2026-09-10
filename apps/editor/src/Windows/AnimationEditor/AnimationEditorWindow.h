#pragma once

#include <QElapsedTimer>
#include <QWidget>
#include <cstdint>
#include <gamelib/BlendSpaceInfo.h>
#include <qcontainerfwd.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <vector>

#include "Render/Renderer.h"
#include "Render/environment.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"
#include "Windows/AnimationEditor/transition_spans.h"
#include "util/follows_project.h"
#include "util/held_open_assets.h"
#include <bgl/InstanceDesc.h>

#include "Windows/AnimationEditor/AnimationPreviewWindow.h"

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QStackedWidget;
class QTabWidget;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class Scrubber;
class TransitionStrip;
class QTimer;
class QToolButton;

struct AnimationEditorWindowDesc
{
	Renderer*                    renderer                = nullptr;
	uint32_t                     initialPreviewInstances = 16;
	bool                         taaEnabled              = true;
	float                        renderScale             = 1.0f;
	float                        taaReconstructionWidth  = 0.4f;
	editor::EnvironmentApplyDesc previewEnv;

	// Builds the preview viewport without a native window. See RenderTargetWindowDesc.
	bool     headless       = false;
	uint32_t headlessWidth  = 256;
	uint32_t headlessHeight = 256;
};

/**
 * The Animation panel: a left properties column (the opened mesh, its `.banim` sources, the clip
 * list, the active clip's metadata) beside the preview viewport, with the transport strip --
 * play/pause, timeline, frame step, speed -- under the viewport it clocks.
 *
 * The panel owns the one clock: a ~60 Hz tick advances the PlaybackTransport by wall time while
 * playing, and every change of the clock lands in the preview via SetTime. The preview's
 * instances are always {clip, phase 0, rate 1}, so the transport's seconds are the whole story.
 */
class AnimationEditorWindow :
	public QWidget,
	public editor::IHoldsAssets,
	public editor::IFollowsProject
{
	Q_OBJECT

public:
	explicit AnimationEditorWindow(QWidget* parent = nullptr, AnimationEditorWindowDesc desc = {});

	/** The open project's Data directory; clears the preview, since its mesh belonged to the last one. */
	void
	SetDataRoot(const QString& dataRoot) override;

	// The project Data root this panel resolves against, empty until a project opens.
	[[nodiscard]] const QString&
	GetDataRoot() const noexcept
	{
		return m_DataRoot;
	}

	/** Forwarded to the preview -- nullptr releases everything it holds. */
	void
	SetAssets(game::AssetManager* assets);

	/**
	 * Leaving the panel closes what it was showing: the dock's tab switching away (or the dock
	 * closing) clears the preview, which releases the acquired assets and every held-open path.
	 * MainWindow drives this from QDockWidget::visibilityChanged -- a tabified dock's widget gets
	 * no hideEvent on a tab switch -- through editor::IsPanelShown, which is what keeps a minimized
	 * window from reading as a panel the user left.
	 */
	void
	SetDockVisible(bool visible);

	/**
	 * The assets this panel is offering right now, absolute: the shown mesh and every `.banim` in
	 * the source dropdown. What the Content Explorer's held-open guard consults -- deleting or
	 * renaming one of these would leave the panel offering a file that is gone, and nothing on
	 * disk records that the panel has it.
	 */
	[[nodiscard]] QStringList
	GetHeldOpenPaths() const override;

	/**
	 * The tier selector's entry for a pose source, and back.
	 *
	 * A mapping and not a cast: the combo's order is a layout decision and the enum's is bgl's. It
	 * is pinned by a test because the two tiers draw the same picture -- a selector wired to the
	 * wrong one looks exactly like a selector wired to the right one.
	 */
	[[nodiscard]] static int
	TierIndexFor(bgl::PoseSource source) noexcept;

	[[nodiscard]] static bgl::PoseSource
	TierSourceAt(int index) noexcept;

protected:
	// A dropped .bmesh lands here while the empty-state prompt is up; the preview handles its own
	// drops once it is the visible page.
	void
	dragEnterEvent(QDragEnterEvent* event) override;
	void
	dragMoveEvent(QDragMoveEvent* event) override;
	void
	dropEvent(QDropEvent* event) override;

	// A hidden panel stops its clock: the viewport is parked by MainWindow anyway, and a timer
	// advancing an unseen animation is sixty wasted posts a second. Re-showing resumes from where
	// the clock stopped.
	void
	hideEvent(QHideEvent* event) override;
	void
	showEvent(QShowEvent* event) override;

private:
	void
	OpenMeshDialog();

	// Reloads the mesh currently shown, played from `animationsRelPath` and blended by
	// `blendRelPath`.
	void
	LoadShownMesh(const QString& animationsRelPath, const QString& blendRelPath = {});

	[[nodiscard]] QWidget*
	BuildPropertiesColumn();

	// The two surfaces over the shared header: one clip watched, or two blended. Foot IK is the
	// third the spec calls for and is not built here.
	[[nodiscard]] QWidget*
	BuildClipTab();

	[[nodiscard]] QWidget*
	BuildBlendTab();

	// The spaces the open set holds, listed. Read-only here: editing them is its own task, and a
	// tab that showed nothing until it could edit would leave the acquire's own work unverifiable.
	[[nodiscard]] QWidget*
	BuildSpaceTab();

	// Offers the sets authored against the live clip set, and opens the chosen one -- which is a
	// reload, since a rig already uploaded refuses a different set.
	void
	SetBlendSets(const QStringList& sets, int activeIndex);

	// Re-lists the spaces and their samples for whatever set is open.
	void
	ShowSpaces(const std::vector<game::BlendSpaceInfo>& spaces);

	// Lists the samples of space `index`, or the empty-state note when there is none.
	void
	SelectSpace(int index);

	// Creates the empty set for the live clip set and opens it.
	void
	CreateBlendSet();

	// Where a set for the live clip set would be written, or empty when there is no clip set or the
	// convention does not cover it. What decides whether creating one is offered at all.
	[[nodiscard]] QString
	CanonicalBlendSetKey() const;

	[[nodiscard]] QWidget*
	BuildTransportBar();

	/**
	 * Pushes the ground group's state into the preview and greys what has nothing to act on.
	 *
	 * *Plant feet* is the whole group: the floor, the solve against it, and the two sliders that
	 * tilt it. One switch rather than a floor and a solve separately, because neither half is worth
	 * anything alone -- an empty floor shows nothing, and there is nothing to plant against without
	 * one. Off, the sliders go insensitive and keep their values, so turning it back on restores
	 * what was set.
	 *
	 * The box holds the state and this is the one place that pushes it, construction included --
	 * two defaults that could disagree is one that eventually does.
	 */
	void
	UpdateGroundControls();

	// One clock tick: advance by the wall time since the last, push into the preview and the UI.
	void
	Tick();

	// Mirrors the clock into the slider and readouts without their signals scrubbing it back.
	void
	SyncTransportUi();

	void
	SetClips(const std::vector<editor::ClipInfo>& clips);

	void
	SelectClip(int index);

	// Stamps the fade the three controls describe and puts the transport on the window that
	// brackets it. Called when any of them changes, and on a tier switch.
	void
	StampTransition();

	// Leaves the transition window and returns the clock to the selected clip.
	void
	ClearTransition();

	// Whether the tier on screen can hold a fade at all, and what the note says when it cannot.
	void
	UpdateTransitionControls();

	AnimationPreviewWindow* m_Preview = nullptr;
	QStackedWidget*         m_Stage   = nullptr;  // the drop prompt, or the viewport + transport

	QLabel*    m_MeshLabel      = nullptr;
	QComboBox* m_SourceSelector = nullptr;  // which .banim is played

	// Where its instances read their pose: posed per instance every frame, or off the rig's
	// shared table.
	QComboBox* m_TierSelector = nullptr;

	// The ground's tilt, in whole degrees. Committed on release, not per tick: the ground is a
	// rebind that moves the temporal epoch, and a drag committing every tick would keep the
	// preview unaccumulated for the whole gesture.
	Scrubber* m_SlopeSlider = nullptr;
	QLabel*   m_SlopeLabel  = nullptr;

	// Which way uphill points, in whole degrees about +Y from +X, and whether the floor is drawn.
	Scrubber*  m_HeadingSlider = nullptr;
	QLabel*    m_HeadingLabel  = nullptr;
	QCheckBox* m_PlantFeet     = nullptr;

	// The instance's own IK weights, in percent: how far the ankle is carried onto the ground, and
	// how far the sole turns onto it. Committed on release like the slope, as one write of both.
	Scrubber* m_IKWeightSlider = nullptr;
	QLabel*   m_IKWeightLabel  = nullptr;
	Scrubber* m_SoleTurnSlider = nullptr;
	QLabel*   m_SoleTurnLabel  = nullptr;

	// Which `.bblend` is open, and the button that writes the first one. A set is the rig's rather
	// than the panel's, so choosing one reloads the mesh.
	QComboBox*   m_BlendSetSelector = nullptr;
	QPushButton* m_CreateBlendSet   = nullptr;

	QTabWidget* m_Surfaces = nullptr;

	// The Space tab: the spaces the open set holds, and the samples of the selected one.
	QWidget*     m_SpaceGroup    = nullptr;
	QComboBox*   m_SpaceSelector = nullptr;
	QListWidget* m_SampleList    = nullptr;
	QLabel*      m_SpaceNote     = nullptr;

	// What the open set resolved to, as the acquire reported it: what the Space tab lists, and
	// what a later task edits.
	std::vector<game::BlendSpaceInfo> m_Spaces;

	// The set the panel has open, empty when none is. Kept because a reload names it again.
	QString m_BlendRelPath;

	// The stamped fade's layout, which the shared strip is redrawn from every tick.
	editor::TransitionLayout m_TransitionLayout;
	QListWidget*             m_ClipList     = nullptr;
	QLabel*                  m_ClipMetadata = nullptr;

	// Previewing a crossfade: which two clips, how long, and the strip that is all three at once.
	// The duration is typed rather than dragged because the question it answers is whether 0.2 s
	// beats 0.35 s, and two values have to be reachable exactly to be compared at all.
	QWidget*         m_TransitionGroup = nullptr;
	QComboBox*       m_FromClip        = nullptr;
	QComboBox*       m_ToClip          = nullptr;
	QDoubleSpinBox*  m_FadeSeconds     = nullptr;
	QCheckBox*       m_BlendEnabled    = nullptr;
	TransitionStrip* m_Strip           = nullptr;
	QLabel*          m_TransitionNote  = nullptr;

	QWidget*        m_TransportBar = nullptr;
	QToolButton*    m_PlayButton   = nullptr;
	QToolButton*    m_StepBack     = nullptr;
	QToolButton*    m_StepForward  = nullptr;
	QDoubleSpinBox* m_Speed        = nullptr;
	QLabel*         m_TimeReadout  = nullptr;

	editor::PlaybackTransport m_Transport;
	QTimer*                   m_Clock = nullptr;
	QElapsedTimer             m_ClockDelta;

	// True while SyncTransportUi writes the controls, so their signals do not scrub the clock.
	bool m_SyncingUi = false;

	QString m_DataRoot;
	QString m_MeshRelPath;  // empty when nothing is shown
};
