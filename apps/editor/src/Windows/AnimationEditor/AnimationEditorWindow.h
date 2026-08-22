#pragma once

#include <QElapsedTimer>
#include <QWidget>

#include "Windows/AnimationEditor/AnimationPreviewWindow.h"

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QStackedWidget;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QCheckBox;
class QListWidget;
class QPushButton;
class TimelineScrubber;
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
class AnimationEditorWindow : public QWidget
{
	Q_OBJECT

public:
	explicit AnimationEditorWindow(QWidget* parent = nullptr, AnimationEditorWindowDesc desc = {});

	/** The open project's Data directory; clears the preview, since its mesh belonged to the last one. */
	void
	SetDataRoot(const QString& dataRoot);

	/** Forwarded to the preview -- nullptr releases everything it holds. */
	void
	SetAssets(game::AssetManager* assets);

	/**
	 * Leaving the panel closes what it was showing: the dock's tab switching away (or the dock
	 * closing) clears the preview, which releases the acquired assets and every held-open path.
	 * MainWindow drives this from QDockWidget::visibilityChanged -- a tabified dock's widget gets
	 * no hideEvent on a tab switch.
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
	HeldOpenPaths() const;

	/**
	 * The timeline slider position for a clock reading, and back: the slider is `tickCount`
	 * integer ticks over the clip's period. Static so the mapping is pinnable without a window.
	 */
	[[nodiscard]] static int
	TimelineTicks(float seconds, float periodSeconds, int tickCount) noexcept;

	[[nodiscard]] static float
	TimelineSeconds(int ticks, float periodSeconds, int tickCount) noexcept;

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

	// Reloads the mesh currently shown, played from `animationsRelPath`.
	void
	LoadShownMesh(const QString& animationsRelPath);

	[[nodiscard]] QWidget*
	BuildPropertiesColumn();

	[[nodiscard]] QWidget*
	BuildTransportBar();

	// One clock tick: advance by the wall time since the last, push into the preview and the UI.
	void
	Tick();

	// Mirrors the clock into the slider and readouts without their signals scrubbing it back.
	void
	SyncTransportUi();

	void
	SetClips(const std::vector<editor::ClipInfo>& clips);

	// Puts the Show Bones box in step with what is previewable right now: the tier on screen and
	// whether the mesh has clips at all. Also what pushes the overlay's state at the viewport, so
	// the box's own checked state stays the user's intent rather than the renderer's.
	void
	SyncBoneOverlayUi();

	void
	SelectClip(int index);

	AnimationPreviewWindow* m_Preview = nullptr;
	QStackedWidget*         m_Stage   = nullptr;  // the drop prompt, or the viewport + transport

	QLabel*    m_MeshLabel      = nullptr;
	QComboBox* m_SourceSelector = nullptr;  // which .banim is played

	// Which tier it is played through: the rig posed live, or the bake made of it.
	QComboBox*   m_TierSelector  = nullptr;
	QPushButton* m_BakeVatButton = nullptr;

	// The rig's skeleton over the preview; only offered on the skinned tier of a mesh with clips.
	QCheckBox* m_ShowBones = nullptr;

	QListWidget* m_ClipList     = nullptr;
	QLabel*      m_ClipMetadata = nullptr;

	QWidget*          m_TransportBar = nullptr;
	QToolButton*      m_PlayButton   = nullptr;
	QToolButton*      m_StepBack     = nullptr;
	QToolButton*      m_StepForward  = nullptr;
	TimelineScrubber* m_Timeline     = nullptr;
	QDoubleSpinBox*   m_Speed        = nullptr;
	QLabel*           m_TimeReadout  = nullptr;

	editor::PlaybackTransport m_Transport;
	QTimer*                   m_Clock = nullptr;
	QElapsedTimer             m_ClockDelta;

	// True while SyncTransportUi writes the controls, so their signals do not scrub the clock.
	bool m_SyncingUi = false;

	// Whether the shown mesh has a clip table -- the other half of what decides the bone overlay.
	bool m_HasClips = false;

	QString m_DataRoot;
	QString m_MeshRelPath;  // empty when nothing is shown
};
