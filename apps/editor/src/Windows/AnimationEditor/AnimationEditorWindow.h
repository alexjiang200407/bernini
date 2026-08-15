#pragma once

#include <QElapsedTimer>
#include <QWidget>

#include "Windows/AnimationEditor/AnimationPreviewWindow.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;

struct AnimationEditorWindowDesc
{
	Renderer*                    renderer                = nullptr;
	uint32_t                     initialPreviewInstances = 16;
	bool                         taaEnabled              = true;
	float                        renderScale             = 1.0f;
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
	 * The timeline slider position for a clock reading, and back: the slider is `tickCount`
	 * integer ticks over the clip's period. Static so the mapping is pinnable without a window.
	 */
	[[nodiscard]] static int
	TimelineTicks(float seconds, float periodSeconds, int tickCount) noexcept;

	[[nodiscard]] static float
	TimelineSeconds(int ticks, float periodSeconds, int tickCount) noexcept;

protected:
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

	void
	SelectClip(int index);

	AnimationPreviewWindow* m_Preview = nullptr;

	QLabel*      m_MeshLabel      = nullptr;
	QComboBox*   m_SourceSelector = nullptr;
	QListWidget* m_ClipList       = nullptr;
	QLabel*      m_ClipMetadata   = nullptr;

	QWidget*        m_TransportBar = nullptr;
	QToolButton*    m_PlayButton   = nullptr;
	QToolButton*    m_StepBack     = nullptr;
	QToolButton*    m_StepForward  = nullptr;
	QSlider*        m_Timeline     = nullptr;
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
