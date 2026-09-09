#include "AnimationEditorWindow.h"

#include "Windows/AnimationEditor/AnimationPreviewWindow.h"
#include "Windows/AnimationEditor/Scrubber.h"
#include "Windows/AnimationEditor/TransitionStrip.h"
#include "Windows/AnimationEditor/foot_ik_weights.h"
#include "Windows/AnimationEditor/playback_writes.h"
#include "Windows/AnimationEditor/transition_spans.h"
#include "util/mesh_drop.h"
#include <algorithm>
#include <assetlib/project_layout.h>
#include <bgl/InstanceDesc.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <qcontainerfwd.h>
#include <qlatin1stringview.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qsizepolicy.h>
#include <qstringliteral.h>
#include <utility>
#include <vector>

namespace
{

	// Where a previewed fade is stamped, and what is shown either side of it. The clock has to be
	// able to sit before t0, so it is not zero; the rest is how much run-up and settle reads.
	constexpr float  c_TransitionStart         = 10.0f;
	constexpr float  c_TransitionLead          = 0.6f;
	constexpr float  c_TransitionTail          = 0.9f;
	constexpr double c_MinFadeSeconds          = 0.01;
	constexpr double c_MaxFadeSeconds          = 4.0;
	constexpr double c_DefaultFadeSeconds      = 0.25;
	constexpr int    c_ClockIntervalMs         = 16;
	constexpr int    c_RevealRepaintDelaysMs[] = { 0, 300 };
}

AnimationEditorWindow::AnimationEditorWindow(QWidget* parent, AnimationEditorWindowDesc desc) :
	QWidget(parent)
{
	auto rt                   = RenderTargetWindowDesc();
	rt.renderer               = desc.renderer;
	rt.initialInstances       = desc.initialPreviewInstances;
	rt.taaEnabled             = desc.taaEnabled;
	rt.renderScale            = desc.renderScale;
	rt.taaReconstructionWidth = desc.taaReconstructionWidth;
	rt.headless               = desc.headless;
	rt.headlessWidth          = desc.headlessWidth;
	rt.headlessHeight         = desc.headlessHeight;

	m_Preview = new AnimationPreviewWindow(this, std::move(rt), std::move(desc.previewEnv));
	m_Preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_Preview->setMinimumSize(256, 256);

	auto* viewportSide   = new QWidget(this);
	auto* viewportLayout = new QVBoxLayout(viewportSide);
	viewportLayout->setContentsMargins(0, 0, 0, 0);
	viewportLayout->setSpacing(0);
	viewportLayout->addWidget(m_Preview, /*stretch*/ 1);
	viewportLayout->addWidget(BuildTransportBar());

	// A page, not an overlay: a label floated over the native Metal surface is at the mercy of
	// its compositing, and a hidden viewport leaves the frame loop entirely.
	auto* prompt = new QLabel(QStringLiteral("Drop a rigged mesh here\n\nor   Open Mesh..."), this);
	prompt->setAlignment(Qt::AlignCenter);
	prompt->setEnabled(false);

	m_Stage = new QStackedWidget(this);
	m_Stage->addWidget(prompt);
	m_Stage->addWidget(viewportSide);

	auto* splitter = new QSplitter(Qt::Horizontal, this);
	splitter->addWidget(BuildPropertiesColumn());
	splitter->addWidget(m_Stage);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(splitter);

	connect(m_Preview, &AnimationPreviewWindow::MeshChanged, this, [this](const QString& relPath) {
		m_MeshRelPath = relPath;
		m_MeshLabel->setText(relPath.isEmpty() ? QStringLiteral("No mesh open") : relPath);
		m_Stage->setCurrentIndex(relPath.isEmpty() ? 0 : 1);
	});

	connect(
		m_Preview,
		&AnimationPreviewWindow::AnimationSourcesChanged,
		this,
		[this](const QStringList& candidates, int activeIndex) {
			m_SyncingUi = true;
			m_SourceSelector->clear();
			m_SourceSelector->addItems(candidates);
			m_SourceSelector->setCurrentIndex(activeIndex);
			m_SourceSelector->setEnabled(candidates.size() > 1);
			m_TierSelector->setEnabled(!candidates.isEmpty());
			m_SyncingUi = false;
		});

	// The tier the panel is *actually* on, which is not always the one just clicked: a switch whose
	// load fails leaves the old tier on screen, and this is what puts the combo back on it.
	connect(
		m_Preview,
		&AnimationPreviewWindow::PoseSourceChanged,
		this,
		[this](const bgl::PoseSource source) {
			m_SyncingUi = true;
			m_TierSelector->setCurrentIndex(TierIndexFor(source));
			m_SyncingUi = false;
			UpdateTransitionControls();
		});

	connect(
		m_Preview,
		&AnimationPreviewWindow::ClipsChanged,
		this,
		&AnimationEditorWindow::SetClips);

	setAcceptDrops(true);

	m_Clock = new QTimer(this);
	m_Clock->setInterval(c_ClockIntervalMs);
	connect(m_Clock, &QTimer::timeout, this, &AnimationEditorWindow::Tick);
}

QWidget*
AnimationEditorWindow::BuildPropertiesColumn()
{
	auto* column = new QWidget(this);
	auto* layout = new QVBoxLayout(column);
	layout->setContentsMargins(4, 4, 4, 4);

	auto* openButton = new QPushButton(QStringLiteral("Open Mesh..."), column);
	connect(openButton, &QPushButton::clicked, this, &AnimationEditorWindow::OpenMeshDialog);

	// The way to let go of the held assets: the explorer refuses to delete or rename what this
	// panel is offering, and "close it first" needs a close to point at.
	auto* closeButton = new QPushButton(QStringLiteral("Close"), column);
	closeButton->setEnabled(false);
	connect(closeButton, &QPushButton::clicked, this, [this] { m_Preview->Clear(); });
	connect(
		m_Preview,
		&AnimationPreviewWindow::MeshChanged,
		closeButton,
		[closeButton](const QString& relPath) { closeButton->setEnabled(!relPath.isEmpty()); });

	auto* fileRow = new QHBoxLayout();
	fileRow->setContentsMargins(0, 0, 0, 0);
	fileRow->addWidget(openButton, /*stretch*/ 1);
	fileRow->addWidget(closeButton);
	layout->addLayout(fileRow);

	m_MeshLabel = new QLabel(QStringLiteral("No mesh open"), column);
	m_MeshLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_MeshLabel->setWordWrap(true);
	layout->addWidget(m_MeshLabel);

	layout->addSpacing(8);
	layout->addWidget(new QLabel(QStringLiteral("Animation Source"), column));

	m_SourceSelector = new QComboBox(column);
	m_SourceSelector->setEnabled(false);
	connect(m_SourceSelector, &QComboBox::activated, this, [this](int index) {
		// A different .banim is a different bake: reload the same mesh naming it, which releases
		// the live geom to zero first -- the eviction that lets gamelib see the new request.
		if (m_SyncingUi || index < 0 || m_MeshRelPath.isEmpty() || m_DataRoot.isEmpty())
			return;
		LoadShownMesh(m_SourceSelector->itemText(index));
	});
	layout->addWidget(m_SourceSelector);

	layout->addSpacing(8);
	layout->addWidget(new QLabel(QStringLiteral("Preview As"), column));

	m_TierSelector = new QComboBox(column);
	m_TierSelector->setEnabled(false);
	m_TierSelector->addItem(QStringLiteral("Skinned"));
	m_TierSelector->addItem(QStringLiteral("Crowd"));
	m_TierSelector->setToolTip(QStringLiteral(
		"Skinned poses the rig every frame, per instance; Crowd reads a pose the rig posed once "
		"and shares. The two draw the same picture -- this is how the crowd path gets exercised, "
		"not something to look for on screen."));
	connect(m_TierSelector, &QComboBox::activated, this, [this](int index) {
		if (m_SyncingUi || index < 0)
			return;
		// The fade goes first. A tier switch respawns onto whatever the record was mostly showing,
		// and leaving a transition puts the clip list's own selection back -- so clearing after the
		// switch respawns twice for one click, onto two different clips.
		ClearTransition();
		m_Preview->SetPoseSource(TierSourceAt(index), m_Transport.GetTimeSeconds());
	});
	layout->addWidget(m_TierSelector);

	layout->addSpacing(8);
	m_SlopeLabel = new QLabel(QStringLiteral("Ground Slope: 0\u00b0"), column);
	layout->addWidget(m_SlopeLabel);

	m_SlopeSlider = new Scrubber(column);
	m_SlopeSlider->SetRange(-30, 30);
	m_SlopeSlider->SetValue(0);
	m_SlopeSlider->setToolTip(QStringLiteral(
		"Tilts the ground the rig stands on. A rig with an avatar plants its feet against it; one "
		"without stands through it. Rises toward +X."));

	// The label follows the thumb so the number is readable mid-drag; the ground follows the
	// release, because moving it moves the temporal epoch and a drag would hold the preview
	// unaccumulated until it ended. A click or a keypress moves the thumb without a drag, and
	// commits through the same release path.
	connect(m_SlopeSlider, &Scrubber::ValueChanged, this, [this](int degrees) {
		m_SlopeLabel->setText(QStringLiteral("Ground Slope: %1\u00b0").arg(degrees));
	});
	connect(m_SlopeSlider, &Scrubber::Committed, this, [this](int degrees) {
		m_Preview->SetGroundSlope(static_cast<float>(degrees));
	});
	layout->addWidget(m_SlopeSlider);

	// Which way uphill points. Nothing in the path knows which way a rig moves -- the test coyote
	// runs along +Z -- so a person turns the hill to face the stride. Committed like the slope.
	m_HeadingLabel = new QLabel(QStringLiteral("Uphill Heading: 0\u00b0"), column);
	layout->addWidget(m_HeadingLabel);

	m_HeadingSlider = new Scrubber(column);
	m_HeadingSlider->SetRange(0, 359);
	m_HeadingSlider->SetValue(0);
	m_HeadingSlider->setToolTip(QStringLiteral(
		"Which way the ground rises, in degrees about the up axis from +X. Turn it to face the way "
		"the rig moves to see it climb the slope rather than cross it."));
	connect(m_HeadingSlider, &Scrubber::ValueChanged, this, [this](int degrees) {
		m_HeadingLabel->setText(QStringLiteral("Uphill Heading: %1\u00b0").arg(degrees));
	});
	connect(m_HeadingSlider, &Scrubber::Committed, this, [this](int degrees) {
		m_Preview->SetGroundHeading(static_cast<float>(degrees));
	});
	layout->addWidget(m_HeadingSlider);

	// One write carries both weights, so either slider's release commits the pair.
	const auto commitFootIK = [this] {
		m_Preview->SetFootIK(
			editor::FootIKForSliders(m_IKWeightSlider->GetValue(), m_SoleTurnSlider->GetValue()));
	};

	m_IKWeightLabel = new QLabel(QStringLiteral("IK Weight: 100%"), column);
	layout->addWidget(m_IKWeightLabel);

	m_IKWeightSlider = new Scrubber(column);
	m_IKWeightSlider->SetRange(0, 100);
	m_IKWeightSlider->SetValue(100);
	m_IKWeightSlider->setToolTip(QStringLiteral(
		"How far each foot is carried onto the ground, over what the clip baked. 0 leaves the "
		"ankle where the animation put it; 100 seats it on the plane."));
	connect(m_IKWeightSlider, &Scrubber::ValueChanged, this, [this](int percent) {
		m_IKWeightLabel->setText(QStringLiteral("IK Weight: %1%").arg(percent));
	});
	connect(m_IKWeightSlider, &Scrubber::Committed, this, commitFootIK);
	layout->addWidget(m_IKWeightSlider);

	m_SoleTurnLabel = new QLabel(QStringLiteral("Sole Turn: 100%"), column);
	layout->addWidget(m_SoleTurnLabel);

	m_SoleTurnSlider = new Scrubber(column);
	m_SoleTurnSlider->SetRange(0, 100);
	m_SoleTurnSlider->SetValue(100);
	m_SoleTurnSlider->setToolTip(QStringLiteral(
		"How far each sole turns onto the slope under it. 0 keeps the foot's authored tilt with "
		"its contact still on the ground; 100 lays it on the slope."));
	connect(m_SoleTurnSlider, &Scrubber::ValueChanged, this, [this](int percent) {
		m_SoleTurnLabel->setText(QStringLiteral("Sole Turn: %1%").arg(percent));
	});
	connect(m_SoleTurnSlider, &Scrubber::Committed, this, commitFootIK);
	layout->addWidget(m_SoleTurnSlider);

	// Off to begin with, so a panel just opened shows the clip as its author left it: the ground is
	// a thing to try, and a preview that silently moved a foot on the way in would be answering a
	// question nobody had asked yet.
	m_PlantFeet = new QCheckBox(QStringLiteral("Plant feet"), column);
	m_PlantFeet->setChecked(false);
	m_PlantFeet->setToolTip(QStringLiteral(
		"Stands the rig on a ground plane and solves each leg onto it. Off, there is no floor and "
		"the clip plays exactly as authored, which is the other half of judging the solve."));
	connect(m_PlantFeet, &QCheckBox::toggled, this, [this] { UpdateGroundControls(); });
	layout->addWidget(m_PlantFeet);

	// What is being done with the clip set, rather than what it is: one clip watched, or two
	// blended. Everything above stays shared -- the ground and the plant switch especially, since a
	// blended plant weight has to be judged on a slope while the blend controls are visible.
	layout->addSpacing(8);
	m_Surfaces = new QTabWidget(column);
	m_Surfaces->addTab(BuildClipTab(), QStringLiteral("Clip"));
	m_Surfaces->addTab(BuildBlendTab(), QStringLiteral("Blend"));
	// The tab decides what is being watched, so leaving Blend puts the clip back and entering it
	// restores whatever fade its controls describe. That is also how a chosen To is undone.
	connect(m_Surfaces, &QTabWidget::currentChanged, this, [this](int) {
		if (m_Surfaces->currentWidget() == m_TransitionGroup)
			StampTransition();
		else
			ClearTransition();
	});
	layout->addWidget(m_Surfaces, /*stretch*/ 1);

	// The box is the state; this is what puts the preview on it. Reaches the preview before it is
	// on screen, where a rebind is recorded and applied when it is shown.
	UpdateGroundControls();

	// The column scrolls rather than asking the window for its height: every control adds to a
	// minimum that would otherwise be taken out of whatever dock sits below the panel. Its width
	// is still its own, though -- a scroll area hides both hints from the splitter, and the
	// horizontal bar is off, so without the floor a narrowed column would clip its buttons.
	auto* scrollBox = new QScrollArea(this);
	scrollBox->setWidget(column);
	scrollBox->setWidgetResizable(true);
	scrollBox->setFrameShape(QFrame::NoFrame);
	scrollBox->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scrollBox->setMinimumWidth(column->sizeHint().width());
	return scrollBox;
}

QWidget*
AnimationEditorWindow::BuildClipTab()
{
	auto* page   = new QWidget(this);
	auto* layout = new QVBoxLayout(page);
	layout->setContentsMargins(4, 4, 4, 4);

	m_ClipList = new QListWidget(page);
	connect(m_ClipList, &QListWidget::currentRowChanged, this, &AnimationEditorWindow::SelectClip);
	layout->addWidget(m_ClipList, /*stretch*/ 1);

	m_ClipMetadata = new QLabel(page);
	m_ClipMetadata->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_ClipMetadata);

	return page;
}

QWidget*
AnimationEditorWindow::BuildBlendTab()
{
	m_TransitionGroup = new QWidget(this);
	auto* fade        = new QVBoxLayout(m_TransitionGroup);
	fade->setContentsMargins(4, 4, 4, 4);

	auto* ends = new QHBoxLayout();
	ends->setContentsMargins(0, 0, 0, 0);
	m_FromClip = new QComboBox(m_TransitionGroup);
	m_ToClip   = new QComboBox(m_TransitionGroup);
	// Nothing to fade to until somebody says so: the panel's resting state is one clip playing, and
	// a second end filled in by default would offer a transition nobody asked for.
	m_ToClip->setPlaceholderText(QStringLiteral("fade to..."));
	ends->addWidget(m_FromClip, /*stretch*/ 1);
	ends->addWidget(new QLabel(QStringLiteral("→"), m_TransitionGroup));
	ends->addWidget(m_ToClip, /*stretch*/ 1);
	fade->addLayout(ends);

	auto* timing = new QHBoxLayout();
	timing->setContentsMargins(0, 0, 0, 0);
	timing->addWidget(new QLabel(QStringLiteral("Fade"), m_TransitionGroup));
	m_FadeSeconds = new QDoubleSpinBox(m_TransitionGroup);
	m_FadeSeconds->setRange(c_MinFadeSeconds, c_MaxFadeSeconds);
	m_FadeSeconds->setSingleStep(0.05);
	m_FadeSeconds->setDecimals(2);
	m_FadeSeconds->setValue(c_DefaultFadeSeconds);
	m_FadeSeconds->setSuffix(QStringLiteral(" s"));
	// Committed rather than tracked: with keyboard tracking on, valueChanged fires per keystroke,
	// and every one of those re-stamps the record and re-parks the clock at the window's start --
	// so typing "0.35" would snap the playhead back three times and lose the scrub position the
	// comparison is being made at.
	m_FadeSeconds->setKeyboardTracking(false);
	timing->addWidget(m_FadeSeconds, /*stretch*/ 1);

	// The comparison the fade has to win. Off, the same two clips meet at the same instant with no
	// blend between them, so what the fade is worth is the difference between two ticks of one box
	// rather than a memory of the last time the panel was open -- the argument Plant feet is on.
	m_BlendEnabled = new QCheckBox(QStringLiteral("Blend"), m_TransitionGroup);
	m_BlendEnabled->setChecked(true);
	timing->addWidget(m_BlendEnabled);
	fade->addLayout(timing);

	m_TransitionNote = new QLabel(m_TransitionGroup);
	m_TransitionNote->setWordWrap(true);
	fade->addWidget(m_TransitionNote);

	// Naming both ends is the request: there is nothing else a chosen From and To could mean here,
	// so a button to confirm it would only be a second click. StampTransition itself decides
	// whether the pair is one -- an unset or matching To leaves the clip playing.
	//
	// Re-stamped from a clean record each time, with the clock parked at the window's start, so no
	// stamp is ever a fade interrupting a live one.
	const auto restamp = [this] { StampTransition(); };
	connect(m_FromClip, &QComboBox::activated, this, [restamp](int) { restamp(); });
	connect(m_ToClip, &QComboBox::activated, this, [restamp](int) { restamp(); });
	connect(m_FadeSeconds, &QDoubleSpinBox::valueChanged, this, [restamp](double) { restamp(); });
	connect(m_BlendEnabled, &QCheckBox::toggled, this, [this, restamp](bool) {
		UpdateTransitionControls();
		restamp();
	});

	fade->addStretch(1);
	return m_TransitionGroup;
}

void
AnimationEditorWindow::UpdateGroundControls()
{
	// One switch for the whole group: the floor, the solve against it, and the four sliders under
	// it. There is nothing to see in a floor nothing stands on, and nothing to plant against
	// without one.
	const bool planting = m_PlantFeet->isChecked();

	m_SlopeLabel->setEnabled(planting);
	m_SlopeSlider->setEnabled(planting);
	m_HeadingLabel->setEnabled(planting);
	m_HeadingSlider->setEnabled(planting);
	m_IKWeightLabel->setEnabled(planting);
	m_IKWeightSlider->setEnabled(planting);
	m_SoleTurnLabel->setEnabled(planting);
	m_SoleTurnSlider->setEnabled(planting);

	m_Preview->SetFloorVisible(planting);
	m_Preview->SetFootPlanting(planting);
}

QWidget*
AnimationEditorWindow::BuildTransportBar()
{
	m_TransportBar  = new QWidget(this);
	QWidget* bar    = m_TransportBar;
	auto*    layout = new QHBoxLayout(bar);
	layout->setContentsMargins(4, 4, 4, 4);

	m_PlayButton = new QToolButton(bar);
	m_PlayButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
	connect(m_PlayButton, &QToolButton::clicked, this, [this] {
		if (m_Transport.IsPlaying())
		{
			m_Transport.Pause();
			m_Clock->stop();
		}
		else
		{
			m_Transport.Play();
			m_ClockDelta.restart();
			m_Clock->start();
		}
		SyncTransportUi();
	});
	layout->addWidget(m_PlayButton);

	m_StepBack = new QToolButton(bar);
	m_StepBack->setIcon(style()->standardIcon(QStyle::SP_MediaSeekBackward));
	connect(m_StepBack, &QToolButton::clicked, this, [this] {
		m_Transport.StepFrames(-1);
		m_Preview->SetTime(m_Transport.GetTimeSeconds());
		SyncTransportUi();
	});
	layout->addWidget(m_StepBack);

	m_StepForward = new QToolButton(bar);
	m_StepForward->setIcon(style()->standardIcon(QStyle::SP_MediaSeekForward));
	connect(m_StepForward, &QToolButton::clicked, this, [this] {
		m_Transport.StepFrames(1);
		m_Preview->SetTime(m_Transport.GetTimeSeconds());
		SyncTransportUi();
	});
	layout->addWidget(m_StepForward);

	// One timeline for both tabs. A clip is the same strip with nothing to fade to -- one bar, no
	// overlap -- so the Clip tab and the Blend tab differ in what the record holds rather than in
	// what draws it.
	m_Strip = new TransitionStrip(bar);
	connect(m_Strip, &TransitionStrip::TimeScrubbed, this, [this](const float seconds) {
		if (m_SyncingUi)
			return;
		m_Transport.Scrub(seconds);
		m_Preview->SetTime(m_Transport.GetTimeSeconds());
		SyncTransportUi();
	});
	layout->addWidget(m_Strip, /*stretch*/ 1);

	m_TimeReadout = new QLabel(bar);
	m_TimeReadout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	layout->addWidget(m_TimeReadout);

	m_Speed = new QDoubleSpinBox(bar);
	m_Speed->setRange(-4.0, 4.0);
	m_Speed->setSingleStep(0.25);
	m_Speed->setValue(1.0);
	m_Speed->setSuffix(QStringLiteral("x"));
	connect(m_Speed, &QDoubleSpinBox::valueChanged, this, [this](double speed) {
		m_Transport.SetSpeed(static_cast<float>(speed));
	});
	layout->addWidget(m_Speed);

	bar->setEnabled(false);
	return bar;
}

void
AnimationEditorWindow::SetDockVisible(const bool visible)
{
	if (!visible)
		m_Preview->Clear();
}

void
AnimationEditorWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (editor::IsMeshDrag(event->mimeData()))
		event->acceptProposedAction();
}

void
AnimationEditorWindow::dragMoveEvent(QDragMoveEvent* event)
{
	if (editor::IsMeshDrag(event->mimeData()))
		event->acceptProposedAction();
}

void
AnimationEditorWindow::dropEvent(QDropEvent* event)
{
	const editor::MeshDrop drop = editor::GetMeshDroppedOn(event->mimeData(), m_DataRoot);
	if (drop.mesh.isEmpty())
	{
		editor::ReportUnresolved(window(), drop);
		return;
	}

	m_Preview->LoadMesh(std::filesystem::path(drop.mesh.toStdWString()));
	event->acceptProposedAction();
}

void
AnimationEditorWindow::SetDataRoot(const QString& dataRoot)
{
	m_DataRoot = dataRoot;
	m_Preview->SetDataRoot(std::filesystem::path(dataRoot.toStdWString()));
	m_Preview->Clear();
}

QStringList
AnimationEditorWindow::GetHeldOpenPaths() const
{
	if (m_DataRoot.isEmpty() || m_MeshRelPath.isEmpty())
		return {};

	const QDir root(m_DataRoot);

	auto held = QStringList();
	held << root.absoluteFilePath(m_MeshRelPath);
	for (int i = 0; i < m_SourceSelector->count(); ++i)
		held << root.absoluteFilePath(m_SourceSelector->itemText(i));
	return held;
}

void
AnimationEditorWindow::SetAssets(game::AssetManager* assets)
{
	m_Preview->SetAssets(assets);

	// The geometry just went with the manager; the clip list, transport and mesh label must not
	// keep advertising it.
	if (assets == nullptr)
		m_Preview->Clear();
}

int
AnimationEditorWindow::TierIndexFor(const bgl::PoseSource source) noexcept
{
	return source == bgl::PoseSource::kPerInstance ? 0 : 1;
}

bgl::PoseSource
AnimationEditorWindow::TierSourceAt(const int index) noexcept
{
	// Only entry 1 names the table; anything else is the hero tier, which is also what an instance
	// gets when nothing sets a source. The combo cannot deliver an out-of-range index, but a
	// mapping whose fallback is the crowd tier would switch tiers on one if it ever did.
	return index == 1 ? bgl::PoseSource::kBoneAnimTable : bgl::PoseSource::kPerInstance;
}

void
AnimationEditorWindow::LoadShownMesh(const QString& animationsRelPath)
{
	const auto absolute = std::filesystem::path(m_DataRoot.toStdWString()) /
	                      std::filesystem::path(m_MeshRelPath.toStdWString());
	m_Preview->LoadMesh(absolute, animationsRelPath.toStdString());
}

void
AnimationEditorWindow::OpenMeshDialog()
{
	auto start = QString();
	if (!m_DataRoot.isEmpty())
		start = m_DataRoot + QLatin1Char('/') + QLatin1String(assetlib::c_MeshesDirectoryName);

	const QString file = QFileDialog::getOpenFileName(
		this,
		QStringLiteral("Open Mesh"),
		start,
		QStringLiteral("Baked Mesh (*.bmesh)"));
	if (file.isEmpty())
		return;

	m_Preview->LoadMesh(std::filesystem::path(file.toStdWString()));
}

void
AnimationEditorWindow::Tick()
{
	if (!m_Transport.IsPlaying())
		return;

	const float dt = static_cast<float>(m_ClockDelta.restart()) / 1000.0f;
	m_Transport.Advance(dt);

	// A one-shot that reached either end is done, not playing a frozen frame; Play rewinds it.
	if (m_Transport.HasClips() && !m_Transport.GetActiveClip().loop &&
	    (m_Transport.GetTimeSeconds() >= m_Transport.GetPeriodSeconds() ||
	     (m_Transport.GetSpeed() < 0.0f && m_Transport.GetTimeSeconds() <= 0.0f)))
	{
		m_Transport.Pause();
		m_Clock->stop();
	}

	m_Preview->SetTime(m_Transport.GetTimeSeconds());
	SyncTransportUi();
}

void
AnimationEditorWindow::StampTransition()
{
	// Only while the Blend tab is the one showing: it is the surface that says a fade is what is
	// being watched, and the Clip tab means the opposite.
	if (m_SyncingUi || !m_Transport.HasClips() ||
	    !editor::RewritesPlayback(m_Preview->GetPoseSource()) ||
	    m_Surfaces->currentWidget() != m_TransitionGroup)
	{
		return;
	}

	const int from = m_FromClip->currentIndex();
	const int to   = m_ToClip->currentIndex();
	if (from < 0 || to < 0 || from == to)
	{
		ClearTransition();
		return;
	}

	// t0 is arbitrary and only has to be somewhere the clock can sit before it, since every ramp is
	// stamped in absolute time and read by moving the clock across them.
	// Unblended, the two clips still meet -- they just meet over one sample interval instead of the
	// authored window, which is a cut. Not a duration of zero: that evicts the outgoing clip from
	// the record and shows the destination across the whole window (editor::CutSeconds).
	const float fadeSeconds =
		m_BlendEnabled->isChecked() ?
			static_cast<float>(m_FadeSeconds->value()) :
			editor::CutSeconds(m_Transport.GetClips().at(static_cast<unsigned>(from)).sampleRate);

	const auto layout =
		editor::WindowFor(c_TransitionStart, fadeSeconds, c_TransitionLead, c_TransitionTail);

	// The clock is parked outside the window *before* the record is written, and the order is the
	// point: SetTime is queued to the render thread while the write below blocks on it, so writing
	// first would leave the render thread free to draw the new fade against the old clock.
	m_Transport.SetTransitionWindow(layout.windowStart, layout.windowEnd);
	m_Preview->SetTime(m_Transport.GetTimeSeconds());

	m_Preview->StampTransition(
		static_cast<uint32_t>(from),
		static_cast<uint32_t>(to),
		layout.start,
		layout.duration);

	m_TransitionLayout = layout;
	m_Strip->SetLayout(layout);
	m_Strip->SetClipNames(m_FromClip->currentText(), m_ToClip->currentText());

	UpdateTransitionControls();
	SyncTransportUi();
}

void
AnimationEditorWindow::ClearTransition()
{
	if (!m_Transport.InTransitionWindow())
		return;

	m_Transport.ClearTransitionWindow();
	m_Preview->SetActiveClip(m_Transport.GetActiveClipIndex(), m_Transport.GetTimeSeconds());
	m_Preview->SetTime(m_Transport.GetTimeSeconds());
	UpdateTransitionControls();
	SyncTransportUi();
}

void
AnimationEditorWindow::UpdateTransitionControls()
{
	const bool rewritable = editor::RewritesPlayback(m_Preview->GetPoseSource());
	const bool playable   = m_Transport.HasClips();
	const bool usable     = rewritable && playable;

	m_FromClip->setEnabled(usable);
	m_ToClip->setEnabled(usable);
	// Nothing to set while the fade is a cut.
	m_FadeSeconds->setEnabled(usable && m_BlendEnabled->isChecked());
	m_BlendEnabled->setEnabled(usable);

	// The strip is live only while a fade is stamped. Left enabled with nothing behind it, a drag
	// would feed the window's absolute seconds to a transport back in clip time, which reads them
	// as that clip's own -- a picture disagreeing with the record, which is the one thing it must
	// never do.
	// The strip stays live whatever the tab: with no fade stamped it is the active clip's own
	// timeline, which is what the Clip tab wants of it.
	const bool live = m_Transport.InTransitionWindow();
	m_Strip->setEnabled(playable);
	if (!live)
	{
		m_TransitionLayout = editor::TransitionLayout();
		m_Strip->SetClipNames(
			m_Transport.HasClips() ? QString::fromStdString(m_Transport.GetActiveClip().name) :
									 QString(),
			QString());
	}

	// Disabled with the reason rather than hidden: a control that vanishes on a tier switch reads
	// as a bug, and this one is a constraint of the tier rather than a missing feature.
	if (!playable)
		m_TransitionNote->setText(QStringLiteral("No clips to fade between."));
	else if (!rewritable)
	{
		m_TransitionNote->setText(QStringLiteral(
			"The shared bone table plays one clip per instance and holds no slots, so there "
			"is nothing to fade between -- it still interpolates frames within that clip. "
			"Switch to the per-instance source to preview a fade."));
	}
	else if (!live)
	{
		m_TransitionNote->setText(QStringLiteral("Playing one clip. Choose what to fade to."));
	}
	else
		m_TransitionNote->clear();

	if (!usable)
		ClearTransition();
}

void
AnimationEditorWindow::SyncTransportUi()
{
	m_SyncingUi = true;

	m_PlayButton->setIcon(
		style()->standardIcon(
			m_Transport.IsPlaying() ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
	// Not while the user holds the handle: a loop's last tick wraps to zero, and writing that
	// back mid-drag snaps the scrubber out from under the cursor.
	// The strip is the timeline in both domains. A transition keeps the layout its stamp produced,
	// since the window's ends and the fade inside it are the stamp's to decide; a single clip is
	// derived fresh, because it is only ever the active clip's period.
	if (!m_Strip->IsScrubbing())
	{
		editor::TransitionLayout layout =
			m_Transport.InTransitionWindow() ?
				m_TransitionLayout :
				editor::WindowForClip(m_Transport.GetPeriodSeconds(), 0.0f);
		layout.time = m_Transport.GetTimeSeconds();
		m_Strip->SetLayout(layout);
	}

	if (const std::optional<float> frame = m_Transport.GetCurrentFrame(); frame)
	{
		m_TimeReadout->setText(QStringLiteral("%1s / frame %2")
		                           .arg(m_Transport.GetTimeSeconds(), 0, 'f', 2)
		                           .arg(*frame, 0, 'f', 1));
	}
	else if (m_Transport.HasClips())
	{
		m_TimeReadout->setText(QStringLiteral("%1s").arg(m_Transport.GetTimeSeconds(), 0, 'f', 2));
	}
	else
	{
		m_TimeReadout->clear();
	}

	m_SyncingUi = false;
}

void
AnimationEditorWindow::SetClips(const std::vector<editor::ClipInfo>& clips)
{
	m_Transport.SetClips(clips);

	m_SyncingUi = true;
	m_ClipList->clear();
	m_FromClip->clear();
	m_ToClip->clear();
	for (const editor::ClipInfo& clip : clips)
	{
		const QString name = QString::fromStdString(clip.name);
		m_ClipList->addItem(name);
		m_FromClip->addItem(name);
		m_ToClip->addItem(name);
	}
	if (!clips.empty())
	{
		m_ClipList->setCurrentRow(0);
		m_FromClip->setCurrentIndex(0);
	}
	// -1 after the fill, which is what the placeholder shows: a clip set arrives with one clip
	// playing and no transition pending.
	m_ToClip->setCurrentIndex(-1);
	m_SyncingUi = false;

	const bool playable = !clips.empty();
	m_TransportBar->setEnabled(playable);
	if (playable)
	{
		m_Transport.Play();
		m_ClockDelta.restart();
		m_Clock->start();
	}
	else
	{
		m_Clock->stop();
		m_ClipMetadata->clear();
	}

	m_Preview->SetTime(0.0f);
	SelectClip(playable ? 0 : -1);
	UpdateTransitionControls();
	SyncTransportUi();
}

void
AnimationEditorWindow::SelectClip(const int index)
{
	if (m_SyncingUi && index >= 0)
		return;

	if (index < 0 || !m_Transport.HasClips() ||
	    index >= static_cast<int>(m_Transport.GetClips().size()))
	{
		m_ClipMetadata->clear();
		return;
	}

	// SelectClip drops the transport's window, so a strip left as it was would still be painting a
	// fade that no longer exists on a clock that no longer means what it did.
	m_Transport.SelectClip(static_cast<uint32_t>(index));
	m_Preview->SetActiveClip(static_cast<uint32_t>(index), m_Transport.GetTimeSeconds());
	m_Preview->SetTime(m_Transport.GetTimeSeconds());
	UpdateTransitionControls();

	const editor::ClipInfo& clip = m_Transport.GetActiveClip();
	m_ClipMetadata->setText(QStringLiteral("%1\n%2 frames @ %3 Hz\n%4 s%5")
	                            .arg(QString::fromStdString(clip.name))
	                            .arg(clip.frameCount)
	                            .arg(clip.sampleRate)
	                            .arg(clip.duration, 0, 'f', 3)
	                            .arg(clip.loop ? QStringLiteral(", loops") : QString()));

	// Pin the readout to this clip's widest string (its end values, in SyncTransportUi's own
	// formats): a label that grows with the digits resizes the slider beside it every tick, and
	// the groove's fill then repaints for geometry the handle has already left.
	const QString widest = QStringLiteral("%1s / frame %2")
	                           .arg(m_Transport.GetPeriodSeconds(), 0, 'f', 2)
	                           .arg(static_cast<double>(clip.frameCount), 0, 'f', 1);
	m_TimeReadout->setMinimumWidth(m_TimeReadout->fontMetrics().horizontalAdvance(widest) + 8);

	SyncTransportUi();
}

void
AnimationEditorWindow::hideEvent(QHideEvent* event)
{
	QWidget::hideEvent(event);
	m_Clock->stop();
}

void
AnimationEditorWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);

	// A tabified dock is revealed by being moved in from its parked geometry, and the raster
	// content beside the preview's native Metal view can keep its pre-reveal pixels until the
	// window next repaints wholesale (which is why switching away and back "fixed" it). Do what
	// that tab switch does: a full-window repaint, deferred twice -- once for the reveal's own
	// layout pass, once after the viewport's resize settle.
	for (const int delayMs : c_RevealRepaintDelaysMs)
		QTimer::singleShot(delayMs, this, [this] { window()->update(); });

	if (m_Transport.IsPlaying())
	{
		m_ClockDelta.restart();
		m_Clock->start();
	}
}
