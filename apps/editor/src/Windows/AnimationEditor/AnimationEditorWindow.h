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
class QGroupBox;
class QLabel;
class QListWidget;
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

	// Offers the sets authored against the live clip set, and opens the chosen one -- which is a
	// reload, since a rig already uploaded refuses a different set.
	void
	SetBlendSets(const QStringList& sets, int activeIndex);

	// Takes the spaces the open set resolved to, which the Blend tab's space row offers.
	void
	ShowSpaces(const std::vector<game::BlendSpaceInfo>& spaces);

	[[nodiscard]] QWidget*
	BuildTransportBar();

	/**
	 * Pushes the ground group's state into the preview and shows or hides what it governs.
	 *
	 * *Plant feet* is the whole group: the floor, the solve against it, and the four sliders that
	 * shape it. One switch rather than a floor and a solve separately, because neither half is worth
	 * anything alone -- an empty floor shows nothing, and there is nothing to plant against without
	 * one. Off, the group collapses to its title and the sliders keep their values, so turning it
	 * back on restores what was set.
	 *
	 * The group holds the state and this is the one place that pushes it, construction included --
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

	/**
	 * Fills the two end combos with the clip set and the space row's combo with the open set's
	 * spaces. A space is never an entry in an end combo: exactly one control says the fade arrives
	 * at a space, so the two can never disagree about the destination.
	 *
	 * Everything is restored by name, because an edit that re-acquires the rig comes back through
	 * here and removing a space moves every space after it. A From whose name is gone falls back to
	 * the playing clip, a To to nothing -- the resting state: one clip playing, no fade pending.
	 */
	void
	RefreshTransitionEnds();

	/**
	 * Ranges the space row's parameter box to the chosen space's authored axis, and hides it when
	 * no space is chosen -- there is then nothing for it to name, which is not a refusal.
	 */
	void
	UpdateParameterBoxes();

	// The space the row's combo names, or null when it names none.
	[[nodiscard]] const game::BlendSpaceInfo*
	ChosenSpace() const;

	// Whether the fade's destination is that space rather than the To combo's clip.
	[[nodiscard]] bool
	SpaceIsDestination() const;

	// The space `node` names, or null when it names a clip or nothing.
	[[nodiscard]] const game::BlendSpaceInfo*
	SpaceForNode(int node) const;

	AnimationPreviewWindow* m_Preview = nullptr;
	QStackedWidget*         m_Stage   = nullptr;  // the drop prompt, or the viewport + transport

	QLabel*    m_MeshLabel      = nullptr;
	QComboBox* m_SourceSelector = nullptr;  // which .banim is played

	// Where its instances read their pose: posed per instance every frame, or off the rig's
	// shared table.
	QComboBox* m_TierSelector = nullptr;

	// The switch is the title of the group it governs, and the body is everything it shows: the
	// four sliders below, hidden with it rather than greyed, since the panel opens with planting
	// off and a resting column would otherwise lead with controls that cannot act.
	QGroupBox* m_PlantFeet  = nullptr;
	QWidget*   m_GroundBody = nullptr;

	// The ground's tilt, in whole degrees. Committed on release, not per tick: the ground is a
	// rebind that moves the temporal epoch, and a drag committing every tick would keep the
	// preview unaccumulated for the whole gesture.
	Scrubber* m_SlopeSlider = nullptr;
	QLabel*   m_SlopeLabel  = nullptr;

	// Which way uphill points, in whole degrees about +Y from +X.
	Scrubber* m_HeadingSlider = nullptr;
	QLabel*   m_HeadingLabel  = nullptr;

	// The instance's own IK weights, in percent: how far the ankle is carried onto the ground, and
	// how far the sole turns onto it. Committed on release like the slope, as one write of both.
	Scrubber* m_IKWeightSlider = nullptr;
	QLabel*   m_IKWeightLabel  = nullptr;
	Scrubber* m_SoleTurnSlider = nullptr;
	QLabel*   m_SoleTurnLabel  = nullptr;

	// Which `.bblend` is open. A set is the rig's rather than the panel's, so choosing one reloads
	// the mesh.
	QComboBox* m_BlendSetSelector = nullptr;

	QTabWidget* m_Surfaces = nullptr;

	int m_SelectedClip = -1;

	// What the open set resolved to, as the acquire reported it: what the Blend tab can fade onto.
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
	QWidget*   m_TransitionGroup = nullptr;
	QComboBox* m_FromEnd         = nullptr;
	QComboBox* m_ToEnd           = nullptr;
	// The clip m_ToEnd last named, kept while the space row takes the destination over and across
	// a re-acquire, so unchecking puts back what was chosen rather than nothing.
	QString m_ToClip;

	// The destination when it is on, in place of m_ToEnd: a checkbox, which space, and where on
	// its axis the fade arrives.
	QCheckBox*       m_SpaceEnabled      = nullptr;
	QComboBox*       m_SpaceEnd          = nullptr;
	QDoubleSpinBox*  m_SpaceEndParameter = nullptr;
	QDoubleSpinBox*  m_FadeSeconds       = nullptr;
	QCheckBox*       m_BlendEnabled      = nullptr;
	TransitionStrip* m_Strip             = nullptr;
	QLabel*          m_TransitionNote    = nullptr;

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
