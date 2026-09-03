#include "AnimationEditorWindow.h"

#include "Windows/AnimationEditor/TimelineScrubber.h"
#include "util/mesh_drop.h"
#include <assetlib/Project.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
	constexpr int c_TimelineTicks           = 1000;
	constexpr int c_ClockIntervalMs         = 16;
	constexpr int c_RevealRepaintDelaysMs[] = { 0, 300 };
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
		m_Preview->SetPoseSource(TierSourceAt(index));
	});
	layout->addWidget(m_TierSelector);

	layout->addSpacing(8);
	m_SlopeLabel = new QLabel(QStringLiteral("Ground Slope: 0\u00b0"), column);
	layout->addWidget(m_SlopeLabel);

	m_SlopeSlider = new QSlider(Qt::Horizontal, column);
	m_SlopeSlider->setRange(-30, 30);
	m_SlopeSlider->setValue(0);
	m_SlopeSlider->setTickPosition(QSlider::TicksBelow);
	m_SlopeSlider->setTickInterval(10);
	m_SlopeSlider->setToolTip(QStringLiteral(
		"Tilts the ground the rig stands on. A rig with an avatar plants its feet against it; one "
		"without stands through it. Rises toward +X."));

	// The label follows the thumb so the number is readable mid-drag; the ground follows the
	// release, because moving it moves the temporal epoch and a drag would hold the preview
	// unaccumulated until it ended. A click or a keypress moves the thumb without a drag, and
	// commits through the same release path.
	connect(m_SlopeSlider, &QSlider::valueChanged, this, [this](int degrees) {
		m_SlopeLabel->setText(QStringLiteral("Ground Slope: %1\u00b0").arg(degrees));
		if (!m_SlopeSlider->isSliderDown())
			m_Preview->SetGroundSlope(static_cast<float>(degrees));
	});
	connect(m_SlopeSlider, &QSlider::sliderReleased, this, [this] {
		m_Preview->SetGroundSlope(static_cast<float>(m_SlopeSlider->value()));
	});
	layout->addWidget(m_SlopeSlider);

	// Which way uphill points. Nothing in the path knows which way a rig moves -- the test coyote
	// runs along +Z -- so a person turns the hill to face the stride. Committed like the slope.
	m_HeadingLabel = new QLabel(QStringLiteral("Uphill Heading: 0\u00b0"), column);
	layout->addWidget(m_HeadingLabel);

	m_HeadingSlider = new QSlider(Qt::Horizontal, column);
	m_HeadingSlider->setRange(0, 359);
	m_HeadingSlider->setValue(0);
	m_HeadingSlider->setTickPosition(QSlider::TicksBelow);
	m_HeadingSlider->setTickInterval(90);
	m_HeadingSlider->setToolTip(QStringLiteral(
		"Which way the ground rises, in degrees about the up axis from +X. Turn it to face the way "
		"the rig moves to see it climb the slope rather than cross it."));
	connect(m_HeadingSlider, &QSlider::valueChanged, this, [this](int degrees) {
		m_HeadingLabel->setText(QStringLiteral("Uphill Heading: %1\u00b0").arg(degrees));
		if (!m_HeadingSlider->isSliderDown())
			m_Preview->SetGroundHeading(static_cast<float>(degrees));
	});
	connect(m_HeadingSlider, &QSlider::sliderReleased, this, [this] {
		m_Preview->SetGroundHeading(static_cast<float>(m_HeadingSlider->value()));
	});
	layout->addWidget(m_HeadingSlider);

	m_ShowFloor = new QCheckBox(QStringLiteral("Show floor"), column);
	m_ShowFloor->setChecked(true);
	m_ShowFloor->setToolTip(QStringLiteral(
		"Draws the ground under the rig, and gates what stands on it: with no floor there is no "
		"slope to see and nothing to plant against."));
	connect(m_ShowFloor, &QCheckBox::toggled, this, [this](bool checked) {
		UpdateGroundControls();
		m_Preview->SetFloorVisible(checked);
	});
	layout->addWidget(m_ShowFloor);

	m_PlantFeet = new QCheckBox(QStringLiteral("Plant feet"), column);
	m_PlantFeet->setChecked(true);
	m_PlantFeet->setToolTip(QStringLiteral(
		"Foot IK. Off, the clip plays as authored against the same ground, so the two can be "
		"compared."));
	connect(m_PlantFeet, &QCheckBox::toggled, this, [this] { UpdateGroundControls(); });
	layout->addWidget(m_PlantFeet);

	layout->addSpacing(8);
	layout->addWidget(new QLabel(QStringLiteral("Clips"), column));

	m_ClipList = new QListWidget(column);
	connect(m_ClipList, &QListWidget::currentRowChanged, this, &AnimationEditorWindow::SelectClip);
	layout->addWidget(m_ClipList, /*stretch*/ 1);

	m_ClipMetadata = new QLabel(column);
	m_ClipMetadata->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_ClipMetadata);

	return column;
}

void
AnimationEditorWindow::UpdateGroundControls()
{
	const bool floor = m_ShowFloor->isChecked();

	m_SlopeLabel->setEnabled(floor);
	m_SlopeSlider->setEnabled(floor);
	m_HeadingLabel->setEnabled(floor);
	m_HeadingSlider->setEnabled(floor);
	m_PlantFeet->setEnabled(floor);

	m_Preview->SetFootPlanting(floor && m_PlantFeet->isChecked());
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

	m_Timeline = new TimelineScrubber(bar);
	m_Timeline->SetTickCount(c_TimelineTicks);
	connect(m_Timeline, &TimelineScrubber::ValueChanged, this, [this](int ticks) {
		if (m_SyncingUi)
			return;
		m_Transport.Scrub(TimelineSeconds(ticks, m_Transport.GetPeriodSeconds(), c_TimelineTicks));
		m_Preview->SetTime(m_Transport.GetTimeSeconds());
		SyncTransportUi();
	});
	layout->addWidget(m_Timeline, /*stretch*/ 1);

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

int
AnimationEditorWindow::TimelineTicks(
	const float seconds,
	const float periodSeconds,
	const int   tickCount) noexcept
{
	if (periodSeconds <= 0.0f)
		return 0;

	const float normalized = std::clamp(seconds / periodSeconds, 0.0f, 1.0f);
	return static_cast<int>(std::lround(normalized * static_cast<float>(tickCount)));
}

float
AnimationEditorWindow::TimelineSeconds(
	const int   ticks,
	const float periodSeconds,
	const int   tickCount) noexcept
{
	if (tickCount <= 0 || periodSeconds <= 0.0f)
		return 0.0f;

	return periodSeconds * static_cast<float>(std::clamp(ticks, 0, tickCount)) /
	       static_cast<float>(tickCount);
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
AnimationEditorWindow::SyncTransportUi()
{
	m_SyncingUi = true;

	m_PlayButton->setIcon(
		style()->standardIcon(
			m_Transport.IsPlaying() ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
	// Not while the user holds the handle: a loop's last tick wraps to zero, and writing that
	// back mid-drag snaps the scrubber out from under the cursor.
	if (!m_Timeline->IsScrubbing())
		m_Timeline->SetValue(TimelineTicks(
			m_Transport.GetTimeSeconds(),
			m_Transport.GetPeriodSeconds(),
			c_TimelineTicks));

	if (m_Transport.HasClips())
	{
		m_TimeReadout->setText(QStringLiteral("%1s / frame %2")
		                           .arg(m_Transport.GetTimeSeconds(), 0, 'f', 2)
		                           .arg(m_Transport.GetCurrentFrame(), 0, 'f', 1));
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
	for (const editor::ClipInfo& clip : clips)
		m_ClipList->addItem(QString::fromStdString(clip.name));
	if (!clips.empty())
		m_ClipList->setCurrentRow(0);
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

	m_Transport.SelectClip(static_cast<uint32_t>(index));
	m_Preview->SetActiveClip(static_cast<uint32_t>(index));
	m_Preview->SetTime(0.0f);

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
