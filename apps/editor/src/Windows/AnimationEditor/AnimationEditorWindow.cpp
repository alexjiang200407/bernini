#include "AnimationEditorWindow.h"

#include "Windows/AnimationEditor/AnimationPreviewWindow.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"
#include "Windows/AnimationEditor/Scrubber.h"
#include "Windows/AnimationEditor/TransitionStrip.h"
#include "Windows/AnimationEditor/blend_edits.h"
#include "Windows/AnimationEditor/blend_sets.h"
#include "Windows/AnimationEditor/foot_ik_weights.h"
#include "Windows/AnimationEditor/playback_writes.h"
#include "Windows/AnimationEditor/transition_spans.h"
#include "util/mesh_drop.h"
#include <algorithm>
#include <assetlib/blend.h>
#include <assetlib/project_layout.h>
#include <bgl/InstanceDesc.h>
#include <cstddef>
#include <exception>
#include <gamelib/BlendSpaceInfo.h>
#include <string>
#include <string_view>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTypes>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <qcontainerfwd.h>
#include <qlatin1stringview.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qsizepolicy.h>
#include <qstringliteral.h>
#include <utility>
#include <vector>

namespace
{
	// What the threshold box shows and steps by, and the rule every insertion is measured against:
	// two samples are "apart" when they display apart, so the box's step *is* the rule (ADR-5).
	constexpr int   c_ParameterDecimals = 2;
	constexpr float c_ParameterStep     = 0.01f;

	// What a new sample sits past the run's last, and what a new space's two samples sit at. A whole
	// unit rather than a step: the point of the default is to be seen and then dragged, and a sample
	// one hundredth past its neighbour is neither.
	constexpr float c_ParameterGap = 1.0f;

	// The cursor's resolution. A Scrubber is integer-valued on a closed range, so this is how finely
	// the bar can address a run rather than anything about the run itself.
	constexpr int c_CursorTicks = 1000;

	// How long the pose takes to reach a parameter the cursor jumped to. Short enough to read as
	// immediate, long enough that a click across the bar is not a pop.
	constexpr float c_CursorGlide = 0.08f;

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
		&AnimationPreviewWindow::BlendSetsChanged,
		this,
		[this](const QStringList& sets, int activeIndex) { SetBlendSets(sets, activeIndex); });

	connect(
		m_Preview,
		&AnimationPreviewWindow::SpacesChanged,
		this,
		[this](const std::vector<game::BlendSpaceInfo>& spaces) { ShowSpaces(spaces); });

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

			// Both tabs hold controls the tier can refuse, and both have to hear about it: a cursor
			// left enabled over a tier that cannot show a space is one whose weights describe a
			// pose nobody is drawing.
			UpdateTransitionControls();
			SyncCursor();
			ShowSelectedSpace();
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
		// the live geom to zero first -- the eviction that lets gamelib see the new request. The
		// blend set goes with it: a set is authored against one clip set and names it, so carrying
		// this one across would name a `.banim` the rig no longer plays.
		if (m_SyncingUi || index < 0 || m_MeshRelPath.isEmpty() || m_DataRoot.isEmpty())
			return;
		LoadShownMesh(m_SourceSelector->itemText(index));
	});
	layout->addWidget(m_SourceSelector);

	// Which spaces the rig carries. In the header rather than in the Space tab, because it is a
	// fact about the clip set on screen, the way the `.banim` above it is -- and because opening
	// one reloads the rig, which is not something a tab switch should ever do.
	layout->addSpacing(8);
	layout->addWidget(new QLabel(QStringLiteral("Blend Set"), column));

	m_BlendSetSelector = new QComboBox(column);
	m_BlendSetSelector->setEnabled(false);
	m_BlendSetSelector->addItem(QStringLiteral("None"));
	m_BlendSetSelector->setToolTip(QStringLiteral(
		"The .bblend whose blend spaces this rig carries. Choosing one reloads the mesh: a rig "
		"already uploaded refuses a set it was not built with."));
	connect(m_BlendSetSelector, &QComboBox::activated, this, [this](int index) {
		if (m_SyncingUi || index < 0 || m_MeshRelPath.isEmpty() || m_DataRoot.isEmpty())
			return;
		// Index 0 is "None", so the sets themselves start at 1.
		LoadShownMesh(
			m_SourceSelector->currentText(),
			index == 0 ? QString() : m_BlendSetSelector->itemText(index));
	});
	layout->addWidget(m_BlendSetSelector);

	m_CreateBlendSet = new QPushButton(QStringLiteral("Create Blend Set"), column);
	m_CreateBlendSet->setEnabled(false);
	m_CreateBlendSet->setToolTip(QStringLiteral(
		"Writes the empty .bblend for this clip set, beside it under Authored, and opens it."));
	connect(m_CreateBlendSet, &QPushButton::clicked, this, &AnimationEditorWindow::CreateBlendSet);
	layout->addWidget(m_CreateBlendSet);

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
	m_Surfaces->addTab(BuildSpaceTab(), QStringLiteral("Space"));
	m_Surfaces->addTab(BuildBlendTab(), QStringLiteral("Blend"));
	// The tab decides what is being watched, so leaving Blend puts the clip back and entering it
	// restores whatever fade its controls describe. That is also how a chosen To is undone.
	connect(m_Surfaces, &QTabWidget::currentChanged, this, [this](int) {
		// Only the Blend tab stamps anything into the record; Clip and Space both leave the clip
		// the list selected playing, which is what clearing restores.
		if (m_Surfaces->currentWidget() == m_TransitionGroup)
		{
			StampTransition();
		}
		else
		{
			ClearTransition();
			ShowSelectedSpace();
		}
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

	// The viewport gets a surface of its own, and that is not cosmetic: a scroll is a blit of the
	// top-level's backing store, and the preview beside this one is `WA_PaintOnScreen` -- a native
	// view Qt does not composite through that store. The bookkeeping then disagrees with what is
	// actually on screen, and the blit lands outside this widget entirely: scrolling here smeared a
	// copy of the *main tab bar*, which is not even inside the scroll area. A native viewport cannot
	// blit past itself. See docs/known_issues.md.
	scrollBox->viewport()->setAttribute(Qt::WA_NativeWindow);

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
AnimationEditorWindow::BuildSpaceTab()
{
	m_SpaceGroup = new QWidget(this);
	auto* layout = new QVBoxLayout(m_SpaceGroup);
	layout->setContentsMargins(4, 4, 4, 4);

	m_SpaceSelector = new QComboBox(m_SpaceGroup);
	m_SpaceSelector->setEnabled(false);
	m_SpaceSelector->setPlaceholderText(QStringLiteral("no spaces"));
	connect(m_SpaceSelector, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (!m_SyncingUi)
			SelectSpace(index);
	});
	layout->addWidget(m_SpaceSelector);

	m_AddSpace    = new QPushButton(QStringLiteral("New"), m_SpaceGroup);
	m_RenameSpace = new QPushButton(QStringLiteral("Rename"), m_SpaceGroup);
	m_RemoveSpace = new QPushButton(QStringLiteral("Delete"), m_SpaceGroup);
	connect(m_AddSpace, &QPushButton::clicked, this, &AnimationEditorWindow::AddSpace);
	connect(m_RenameSpace, &QPushButton::clicked, this, &AnimationEditorWindow::RenameSpace);
	connect(m_RemoveSpace, &QPushButton::clicked, this, &AnimationEditorWindow::RemoveSpace);

	auto* spaceRow = new QHBoxLayout();
	spaceRow->addWidget(m_SpaceSelector, /*stretch*/ 1);
	spaceRow->addWidget(m_AddSpace);
	spaceRow->addWidget(m_RenameSpace);
	spaceRow->addWidget(m_RemoveSpace);
	layout->addLayout(spaceRow);

	// Clip and threshold per row, in parameter order -- Unity's Motion list, which is what ADR-1
	// chose over a canvas.
	m_SampleList = new QListWidget(m_SpaceGroup);
	connect(m_SampleList, &QListWidget::currentRowChanged, this, [this](int) {
		if (!m_SyncingUi)
			UpdateSpaceControls();
	});
	layout->addWidget(m_SampleList, /*stretch*/ 1);

	m_SampleClip   = new QComboBox(m_SpaceGroup);
	m_AddSample    = new QPushButton(QStringLiteral("Add"), m_SpaceGroup);
	m_RemoveSample = new QPushButton(QStringLiteral("Remove"), m_SpaceGroup);
	connect(m_AddSample, &QPushButton::clicked, this, &AnimationEditorWindow::AddSample);
	connect(m_RemoveSample, &QPushButton::clicked, this, &AnimationEditorWindow::RemoveSample);
	connect(m_SampleClip, &QComboBox::currentIndexChanged, this, [this](int) {
		if (!m_SyncingUi)
			UpdateSpaceControls();
	});

	auto* sampleRow = new QHBoxLayout();
	sampleRow->addWidget(m_SampleClip, /*stretch*/ 1);
	sampleRow->addWidget(m_AddSample);
	sampleRow->addWidget(m_RemoveSample);
	layout->addLayout(sampleRow);

	m_SampleParameter = new QDoubleSpinBox(m_SpaceGroup);
	m_SampleParameter->setDecimals(c_ParameterDecimals);
	m_SampleParameter->setSingleStep(c_ParameterStep);
	// Wide enough that no finite parameter is clamped on its way into the box: a `.bblend` written
	// by hand can carry anything, and a box that clamped would show a number the document does not
	// hold.
	m_SampleParameter->setRange(
		-static_cast<double>(std::numeric_limits<float>::max()),
		static_cast<double>(std::numeric_limits<float>::max()));
	m_SampleParameter->setKeyboardTracking(false);

	// Live while it moves, written when the edit ends: the pose under a dragged threshold is the
	// whole argument for authoring here (ADR-3), and a file rewritten per keystroke is not.
	connect(m_SampleParameter, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		if (!m_SyncingUi)
			RetargetSample(static_cast<float>(value));
	});
	connect(m_SampleParameter, &QDoubleSpinBox::editingFinished, this, [this] {
		// editingFinished fires on losing focus as well, so an unmoved threshold would otherwise
		// rewrite the whole document every time the box was clicked away from.
		if (!m_SyncingUi && m_BlendSetDirty)
			CommitBlendSet();
	});

	auto* thresholdRow = new QHBoxLayout();
	thresholdRow->addWidget(new QLabel(QStringLiteral("Threshold"), m_SpaceGroup));
	thresholdRow->addWidget(m_SampleParameter, /*stretch*/ 1);
	layout->addLayout(thresholdRow);

	m_FromSpeed = new QPushButton(QStringLiteral("Thresholds from speed"), m_SpaceGroup);
	m_FromSpeed->setToolTip(
		QStringLiteral("Take every threshold from the speed its clip was animated at."));
	connect(m_FromSpeed, &QPushButton::clicked, this, &AnimationEditorWindow::ThresholdsFromSpeed);
	layout->addWidget(m_FromSpeed);

	auto* cursorLine = new QFrame(m_SpaceGroup);
	cursorLine->setFrameShape(QFrame::HLine);
	cursorLine->setFrameShadow(QFrame::Sunken);
	layout->addWidget(cursorLine);

	m_CursorLabel = new QLabel(m_SpaceGroup);
	layout->addWidget(m_CursorLabel);

	m_SpaceCursor = new Scrubber(m_SpaceGroup);
	m_SpaceCursor->SetRange(0, c_CursorTicks);
	connect(m_SpaceCursor, &Scrubber::ValueChanged, this, [this](const int tick) {
		if (!m_SyncingUi)
			MoveCursor(tick);
	});
	layout->addWidget(m_SpaceCursor);

	m_SpaceWeights = new QLabel(m_SpaceGroup);
	m_SpaceWeights->setWordWrap(true);
	layout->addWidget(m_SpaceWeights);

	m_SpaceNote = new QLabel(m_SpaceGroup);
	m_SpaceNote->setWordWrap(true);
	m_SpaceNote->setEnabled(false);
	layout->addWidget(m_SpaceNote);

	return m_SpaceGroup;
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
AnimationEditorWindow::LoadShownMesh(const QString& animationsRelPath, const QString& blendRelPath)
{
	const auto absolute = std::filesystem::path(m_DataRoot.toStdWString()) /
	                      std::filesystem::path(m_MeshRelPath.toStdWString());
	m_Preview->LoadMesh(absolute, animationsRelPath.toStdString(), blendRelPath.toStdString());
}

void
AnimationEditorWindow::SetBlendSets(const QStringList& sets, const int activeIndex)
{
	m_SyncingUi = true;
	m_BlendSetSelector->clear();
	m_BlendSetSelector->addItem(QStringLiteral("None"));
	m_BlendSetSelector->addItems(sets);
	m_BlendSetSelector->setCurrentIndex(activeIndex < 0 ? 0 : activeIndex + 1);
	m_BlendSetSelector->setEnabled(!m_MeshRelPath.isEmpty());
	m_BlendRelPath = activeIndex < 0 ? QString() : sets.at(activeIndex);

	// Nothing to create a set *for* until a clip set is playing, and nothing to create when the
	// convention's key is already taken -- which is every set this panel wrote. The create itself
	// refuses that too; this is what stops it being offered as a button that only ever warns.
	m_CreateBlendSet->setEnabled(
		!m_MeshRelPath.isEmpty() && m_SourceSelector->currentIndex() >= 0 &&
		!sets.contains(CanonicalBlendSetKey()));
	m_SyncingUi = false;
}

QString
AnimationEditorWindow::CanonicalBlendSetKey() const
{
	if (m_SourceSelector->currentIndex() < 0)
		return {};

	const QByteArray animations = m_SourceSelector->currentText().toUtf8();

	try
	{
		return QString::fromStdString(
			assetlib::blendSetKeyFor(
				std::string_view(animations.constData(), static_cast<size_t>(animations.size()))));
	}
	catch (const std::exception&)
	{
		// A clip set somewhere the convention does not cover. Nothing can be created for it, which
		// is what an empty key says to the one caller.
		return {};
	}
}

void
AnimationEditorWindow::ShowSpaces(const std::vector<game::BlendSpaceInfo>& spaces)
{
	m_Spaces = spaces;

	// The document behind them, which is what is edited and saved. A set that will not read leaves
	// the tab listing what the rig resolved and refusing every edit -- the acquire already took it,
	// so an empty list here would be the panel disagreeing with the pose on screen.
	m_BlendSet = assetlib::BlendSet();
	if (!m_BlendRelPath.isEmpty() && !m_DataRoot.isEmpty())
	{
		const QByteArray key = m_BlendRelPath.toUtf8();
		try
		{
			m_BlendSet = editor::LoadBlendSet(
				std::filesystem::path(m_DataRoot.toStdWString()),
				std::string_view(key.constData(), static_cast<size_t>(key.size())));
		}
		catch (const std::exception& e)
		{
			qWarning("AnimationEditor: cannot edit '%s': %s", key.constData(), e.what());
		}
	}

	// What the rig was uploaded with, kept so an edit can be compared against it. Taken here rather
	// than at the save: this runs on every acquire, which is the only thing that moves the rig.
	m_AcquiredSpaces = m_BlendSet.spaces;

	m_SyncingUi = true;
	m_SpaceSelector->clear();
	for (const game::BlendSpaceInfo& space : spaces)
		m_SpaceSelector->addItem(QString::fromStdString(space.name));
	m_SpaceSelector->setEnabled(!spaces.empty());

	// By name, because an added or removed space moves every index after it and the author was
	// editing a space rather than a position. Adding a sample reloads the mesh (ADR-3), so without
	// this the selector walks back to the first space on every edit.
	const int restored = m_SpaceSelector->findText(m_SelectedSpace);
	m_SyncingUi        = false;

	SelectSpace(spaces.empty() ? -1 : std::max(restored, 0));
}

void
AnimationEditorWindow::SelectSpace(const int index)
{
	m_SampleList->clear();

	if (index < 0 || static_cast<size_t>(index) >= m_Spaces.size())
	{
		m_SpaceNote->setText(
			m_BlendRelPath.isEmpty() ?
				QStringLiteral("No blend set open. Choose one above, or create the first.") :
				QStringLiteral("This set holds no blend spaces yet."));
		UpdateSpaceControls();
		return;
	}

	const game::BlendSpaceInfo& space = m_Spaces[static_cast<size_t>(index)];
	m_SelectedSpace                   = QString::fromStdString(space.name);

	for (const game::BlendSpaceSampleInfo& sample : space.samples)
	{
		// The clip by name rather than by index: an index is what the acquire resolved to, and the
		// name is what the `.bblend` says and what a person recognizes.
		const QString clip = sample.clipIndex < static_cast<uint32_t>(m_ClipList->count()) ?
		                         m_ClipList->item(static_cast<int>(sample.clipIndex))->text() :
		                         QStringLiteral("<clip %1>").arg(sample.clipIndex);

		m_SampleList->addItem(QStringLiteral("%1    %2")
		                          .arg(clip)
		                          .arg(sample.parameter, 0, 'f', c_ParameterDecimals));
	}

	const int wanted   = m_PendingSampleRow >= 0 ? m_PendingSampleRow : 0;
	m_PendingSampleRow = -1;

	m_SyncingUi = true;
	m_SampleList->setCurrentRow(
		space.samples.empty() ? -1 : std::min(wanted, static_cast<int>(space.samples.size()) - 1));
	m_SyncingUi = false;

	m_SpaceNote->setText(QStringLiteral("Parameter %1 to %2")
	                         .arg(space.ParameterMin(), 0, 'f', c_ParameterDecimals)
	                         .arg(space.ParameterMax(), 0, 'f', c_ParameterDecimals));

	UpdateSpaceControls();
	SyncCursor();
	ShowSelectedSpace();
}

void
AnimationEditorWindow::UpdateSpaceControls()
{
	const int             index = m_SpaceSelector->currentIndex();
	assetlib::BlendSpace* space = EditedSpace(index);
	const bool            open  = !m_BlendRelPath.isEmpty() && !m_DataRoot.isEmpty();

	// The document is what an edit writes, so an open set whose document would not read leaves the
	// tab listing what the rig resolved and editing nothing.
	const bool editable = open && m_BlendSet.spaces.size() == m_Spaces.size();

	ShowSampleClips();

	m_AddSpace->setEnabled(editable && LoopingClipCount() >= 2);
	m_AddSpace->setToolTip(
		editable && LoopingClipCount() < 2 ?
			QStringLiteral("A blend space needs two looping clips; this set has fewer.") :
			QString());
	m_RenameSpace->setEnabled(editable && space != nullptr);
	m_RemoveSpace->setEnabled(editable && space != nullptr);

	const int  row = m_SampleList->currentRow();
	const bool onSample =
		space != nullptr && row >= 0 && static_cast<size_t>(row) < space->samples.size();

	m_SampleClip->setEnabled(editable && space != nullptr);
	m_AddSample->setEnabled(editable && space != nullptr && m_SampleClip->currentIndex() >= 0);
	m_RemoveSample->setEnabled(editable && onSample && editor::CanRemoveSample(space->samples));
	m_SampleParameter->setEnabled(editable && onSample);

	m_SyncingUi = true;
	if (onSample)
		m_SampleParameter->setValue(
			static_cast<double>(space->samples[static_cast<size_t>(row)].parameter));
	m_SyncingUi = false;
}

void
AnimationEditorWindow::ShowSampleClips()
{
	// Saved and restored rather than set and cleared: this runs inside UpdateSpaceControls, which is
	// itself reached from a sync, and clearing here would let the rest of that sync fire signals.
	const bool syncing = m_SyncingUi;
	m_SyncingUi        = true;

	const QString wanted = m_SampleClip->currentText();
	m_SampleClip->clear();

	auto* model = qobject_cast<QStandardItemModel*>(m_SampleClip->model());
	for (const editor::ClipInfo& clip : m_Transport.GetClips())
	{
		m_SampleClip->addItem(QString::fromStdString(clip.name));

		const std::string_view reason = editor::ClipRefusalReason(clip);
		if (reason.empty() || model == nullptr)
			continue;

		// Listed and disabled rather than dropped: the author is looking for the clip, and an
		// absence would read as a bad clip set rather than as one a blend space cannot hold.
		if (QStandardItem* item = model->item(m_SampleClip->count() - 1); item != nullptr)
		{
			item->setEnabled(false);
			item->setToolTip(
				QString::fromUtf8(reason.data(), static_cast<qsizetype>(reason.size())));
		}
	}

	const int restored = m_SampleClip->findText(wanted);
	m_SampleClip->setCurrentIndex(restored >= 0 ? restored : NthLoopingClip(0));

	m_SyncingUi = syncing;
}

int
AnimationEditorWindow::LoopingClipCount() const
{
	return static_cast<int>(
		std::ranges::count_if(m_Transport.GetClips(), [](const editor::ClipInfo& clip) {
			return editor::ClipRefusalReason(clip).empty();
		}));
}

int
AnimationEditorWindow::NthLoopingClip(const int n) const
{
	int seen = 0;
	for (size_t i = 0; i < m_Transport.GetClips().size(); ++i)
	{
		if (!editor::ClipRefusalReason(m_Transport.GetClips()[i]).empty())
			continue;

		if (seen++ == n)
			return static_cast<int>(i);
	}

	return -1;
}

assetlib::BlendSpace*
AnimationEditorWindow::EditedSpace(const int index)
{
	if (index < 0 || static_cast<size_t>(index) >= m_BlendSet.spaces.size())
		return nullptr;

	return &m_BlendSet.spaces[static_cast<size_t>(index)];
}

void
AnimationEditorWindow::CommitBlendSet()
{
	if (m_BlendRelPath.isEmpty() || m_DataRoot.isEmpty())
		return;

	const QByteArray key = m_BlendRelPath.toUtf8();
	try
	{
		editor::SaveBlendSet(
			std::filesystem::path(m_DataRoot.toStdWString()),
			std::string_view(key.constData(), static_cast<size_t>(key.size())),
			m_BlendSet);
	}
	catch (const std::exception& e)
	{
		// The edit stays on screen: it is the author's, and reverting it would throw away the work
		// rather than the mistake.
		QMessageBox::warning(window(), QStringLiteral("Blend Set"), QString::fromUtf8(e.what()));
		return;
	}

	m_BlendSetDirty = false;

	// ADR-3's fork, asked of the document rather than of the caller: anything but a threshold that
	// moved is a node table that has to be built again, and the reload is the one opening a set
	// already performs. Computed here so a caller cannot take the wrong branch by naming it.
	if (!editor::IsParameterMove(m_AcquiredSpaces, m_BlendSet.spaces))
	{
		LoadShownMesh(m_SourceSelector->currentText(), m_BlendRelPath);
		return;
	}

	// Nothing but thresholds moved, so the rig already uploaded takes them where it stands.
	if (!editor::ApplyParameters(m_BlendSet.spaces, m_Spaces))
		return;

	const QString refusal = m_Preview->RetargetBlendParameters(m_Spaces);
	if (!refusal.isEmpty())
		qWarning("AnimationEditor: a threshold did not go live: %s", qUtf8Printable(refusal));
}

void
AnimationEditorWindow::AddSpace()
{
	bool          accepted = false;
	const QString name     = QInputDialog::getText(
		window(),
		QStringLiteral("New Blend Space"),
		QStringLiteral("Name"),
		QLineEdit::Normal,
		QString(),
		&accepted);

	if (!accepted)
		return;

	const std::string wanted = name.trimmed().toStdString();
	if (!editor::CanNameSpace(m_BlendSet.spaces, wanted))
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("New Blend Space"),
			QStringLiteral("A space needs a name of its own; '%1' is empty or already taken.")
				.arg(name));
		return;
	}

	// Two samples, because one is a clip and every clip is already a node under its own name --
	// `validateBlendSet` refuses a shorter run, so there is no such thing as an empty space to add
	// and fill in. The first two looping clips at 0 and 1 are a run to edit, not a guess at intent.
	auto space = assetlib::BlendSpace();
	space.name = wanted;
	for (int n = 0; n < 2; ++n)
	{
		const int clip = NthLoopingClip(n);
		if (clip < 0)
			return;

		space.samples.emplace_back(
			m_Transport.GetClips()[static_cast<size_t>(clip)].name,
			static_cast<float>(n) * c_ParameterGap);
	}

	m_BlendSet.spaces.push_back(std::move(space));
	CommitBlendSet();
}

void
AnimationEditorWindow::RemoveSpace()
{
	const int index = m_SpaceSelector->currentIndex();
	if (EditedSpace(index) == nullptr)
		return;

	m_BlendSet.spaces.erase(m_BlendSet.spaces.begin() + static_cast<ptrdiff_t>(index));
	CommitBlendSet();
}

void
AnimationEditorWindow::RenameSpace()
{
	const assetlib::BlendSpace* selected = EditedSpace(m_SpaceSelector->currentIndex());
	if (selected == nullptr)
		return;

	bool          accepted = false;
	const QString name     = QInputDialog::getText(
		window(),
		QStringLiteral("Rename Blend Space"),
		QStringLiteral("Name"),
		QLineEdit::Normal,
		QString::fromStdString(selected->name),
		&accepted);

	if (!accepted)
		return;

	// Resolved again rather than carried across the dialog: getText runs a nested event loop, and a
	// pointer into the document cannot be trusted to survive one.
	assetlib::BlendSpace* space = EditedSpace(m_SpaceSelector->currentIndex());
	if (space == nullptr)
		return;

	const std::string wanted = name.trimmed().toStdString();
	if (wanted == space->name)
		return;

	if (!editor::CanNameSpace(m_BlendSet.spaces, wanted))
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Rename Blend Space"),
			QStringLiteral("A space needs a name of its own; '%1' is empty or already taken.")
				.arg(name));
		return;
	}

	space->name = wanted;

	// A rename moves no node, but the name is what the selector and every later resolve read, so the
	// rig is built again rather than left disagreeing with the document.
	CommitBlendSet();
}

void
AnimationEditorWindow::AddSample()
{
	assetlib::BlendSpace* space = EditedSpace(m_SpaceSelector->currentIndex());
	const int             index = m_SampleClip->currentIndex();
	if (space == nullptr || index < 0 ||
	    static_cast<size_t>(index) >= m_Transport.GetClips().size())
	{
		return;
	}

	const editor::ClipInfo& clip = m_Transport.GetClips()[static_cast<size_t>(index)];

	// The combo lists a one-shot disabled rather than hiding it, so the refusal is still owed here:
	// a selection is restored by name when the clip set changes under it, and a name can come back
	// on a clip that no longer loops.
	if (const std::string_view reason = editor::ClipRefusalReason(clip); !reason.empty())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Add Sample"),
			QStringLiteral("'%1' %2.")
				.arg(QString::fromStdString(clip.name))
				.arg(QString::fromUtf8(reason.data(), static_cast<qsizetype>(reason.size()))));
		return;
	}

	// Past the last sample: the end of the run is the one place a new sample always fits, whatever
	// the run already holds.
	const float parameter =
		space->samples.empty() ? 0.0f : space->samples.back().parameter + c_ParameterGap;

	if (!editor::CanInsertAt(space->samples, parameter, c_ParameterStep))
	{
		// Reachable off a run authored by hand, where the last parameter is large enough that one
		// gap past it is the same number again.
		QMessageBox::warning(
			window(),
			QStringLiteral("Add Sample"),
			QStringLiteral("There is no room past the last sample for another threshold."));
		return;
	}

	const size_t at = editor::InsertionIndex(space->samples, parameter);

	// Where the reload should leave the cursor: the author added this sample to work on it.
	m_PendingSampleRow = static_cast<int>(at);

	space->samples.insert(
		space->samples.begin() + static_cast<ptrdiff_t>(at),
		{ clip.name, parameter });

	CommitBlendSet();
}

void
AnimationEditorWindow::RemoveSample()
{
	assetlib::BlendSpace* space = EditedSpace(m_SpaceSelector->currentIndex());
	const int             row   = m_SampleList->currentRow();
	if (space == nullptr || row < 0 || static_cast<size_t>(row) >= space->samples.size())
		return;

	if (!editor::CanRemoveSample(space->samples))
		return;

	space->samples.erase(space->samples.begin() + static_cast<ptrdiff_t>(row));
	CommitBlendSet();
}

void
AnimationEditorWindow::RetargetSample(const float parameter)
{
	assetlib::BlendSpace* space = EditedSpace(m_SpaceSelector->currentIndex());
	const int             row   = m_SampleList->currentRow();
	if (space == nullptr || row < 0 || static_cast<size_t>(row) >= space->samples.size())
		return;

	// Held between its neighbours rather than reordered: a row that jumped position mid-drag would
	// move the thing under the cursor.
	const float held = editor::ClampedParameter(
		space->samples,
		static_cast<size_t>(row),
		parameter,
		c_ParameterStep);

	space->samples[static_cast<size_t>(row)].parameter = held;
	m_BlendSetDirty                                    = true;

	if (editor::ApplyParameters(m_BlendSet.spaces, m_Spaces))
	{
		const QString refusal = m_Preview->RetargetBlendParameters(m_Spaces);
		if (!refusal.isEmpty())
			qWarning("AnimationEditor: a threshold did not go live: %s", qUtf8Printable(refusal));
	}

	// The run's extent may have moved with it, so the cursor is re-ranged and not merely re-read --
	// and the weights beneath it are a different pair of clips once a neighbour crosses it.
	SyncCursor();

	// The list carries the value, so it is redrawn as the threshold moves rather than at the save.
	m_SyncingUi = true;
	if (QListWidgetItem* item = m_SampleList->item(row); item != nullptr)
		item->setText(
			QStringLiteral("%1    %2")
				.arg(QString::fromStdString(space->samples[static_cast<size_t>(row)].clip))
				.arg(held, 0, 'f', c_ParameterDecimals));
	m_SampleParameter->setValue(static_cast<double>(held));
	m_SyncingUi = false;
}

void
AnimationEditorWindow::ShowSelectedSpace()
{
	const int index = m_SpaceSelector->currentIndex();
	if (m_Surfaces->currentWidget() != m_SpaceGroup || index < 0 ||
	    static_cast<size_t>(index) >= m_Spaces.size())
	{
		// Off the Space tab the clip list is what is watched again, which is what leaving Blend
		// already does.
		if (m_Surfaces->currentWidget() != m_SpaceGroup && m_ClipList->currentRow() >= 0)
			m_Preview->SetActiveClip(
				static_cast<uint32_t>(m_ClipList->currentRow()),
				m_Transport.GetTimeSeconds());
		return;
	}

	m_Preview->ShowSpace(
		static_cast<uint32_t>(index),
		m_SpaceParameter,
		m_Transport.GetTimeSeconds());

	ShowCursorWeights();
}

void
AnimationEditorWindow::MoveCursor(const int tick)
{
	const int index = m_SpaceSelector->currentIndex();
	if (index < 0 || static_cast<size_t>(index) >= m_Spaces.size())
		return;

	const game::BlendSpaceInfo& space = m_Spaces[static_cast<size_t>(index)];
	m_SpaceParameter =
		editor::ParameterForTick(space.ParameterMin(), space.ParameterMax(), c_CursorTicks, tick);

	// Retargeted rather than restamped: the slot's phase is rebased onto now first, so the pose
	// keeps the cycle it was already walking instead of jumping on the frame of the write.
	m_Preview->RetargetSpace(
		static_cast<uint32_t>(index),
		m_SpaceParameter,
		m_Transport.GetTimeSeconds(),
		c_CursorGlide);

	ShowCursorWeights();
}

void
AnimationEditorWindow::SyncCursor()
{
	const int  index      = m_SpaceSelector->currentIndex();
	const bool selected   = index >= 0 && static_cast<size_t>(index) < m_Spaces.size();
	const bool rewritable = editor::RewritesPlayback(m_Preview->GetPoseSource());
	const bool shown      = selected && rewritable;

	m_SpaceCursor->setEnabled(shown);
	m_FromSpeed->setEnabled(selected);

	// Disabled with the reason rather than hidden, exactly as the Blend tab's controls are: this is
	// a constraint of the tier, and a control that vanishes on a tier switch reads as a bug. The
	// thresholds stay editable either way -- authoring a space does not need one on screen.
	m_SpaceWeights->setText(
		selected && !rewritable ?
			QStringLiteral(
				"The shared bone table plays one clip per instance and holds no slots, "
				"so a blend space cannot be shown on it. Switch to the per-instance "
				"source to watch one.") :
			QString());

	if (!shown)
	{
		m_CursorLabel->clear();
		return;
	}

	const game::BlendSpaceInfo& space = m_Spaces[static_cast<size_t>(index)];

	// Held inside the run the cursor now addresses: a space selected after another one leaves the
	// parameter somewhere its range may not reach.
	m_SpaceParameter = std::clamp(m_SpaceParameter, space.ParameterMin(), space.ParameterMax());

	m_SyncingUi = true;
	m_SpaceCursor->SetValue(
		editor::TickForParameter(
			space.ParameterMin(),
			space.ParameterMax(),
			c_CursorTicks,
			m_SpaceParameter));
	m_SyncingUi = false;

	ShowCursorWeights();
}

void
AnimationEditorWindow::ShowCursorWeights()
{
	const int index = m_SpaceSelector->currentIndex();
	if (index < 0 || static_cast<size_t>(index) >= m_Spaces.size() ||
	    !editor::RewritesPlayback(m_Preview->GetPoseSource()))
	{
		// The tier's refusal is already in this label and is the truer thing to say: weights under
		// a cursor mean nothing while no pose is being blended from them.
		return;
	}

	const game::BlendSpaceInfo&    space = m_Spaces[static_cast<size_t>(index)];
	const game::BlendSpaceStraddle at    = space.StraddleAt(m_SpaceParameter);

	m_CursorLabel->setText(
		QStringLiteral("Parameter %1").arg(m_SpaceParameter, 0, 'f', c_ParameterDecimals));

	const auto clipName = [this, &space](const size_t sample) {
		const uint32_t clip = space.samples[sample].clipIndex;
		return clip < static_cast<uint32_t>(m_ClipList->count()) ?
		           m_ClipList->item(static_cast<int>(clip))->text() :
		           QStringLiteral("<clip %1>").arg(clip);
	};

	// Both ends name the same sample outside the authored range, which is that clip playing alone.
	if (at.lower == at.upper)
	{
		m_SpaceWeights->setText(QStringLiteral("%1  100%").arg(clipName(at.lower)));
		return;
	}

	m_SpaceWeights->setText(QStringLiteral("%1  %2%     %3  %4%")
	                            .arg(clipName(at.lower))
	                            .arg((1.0f - at.weight) * 100.0f, 0, 'f', 0)
	                            .arg(clipName(at.upper))
	                            .arg(at.weight * 100.0f, 0, 'f', 0));
}

void
AnimationEditorWindow::ThresholdsFromSpeed()
{
	assetlib::BlendSpace* space = EditedSpace(m_SpaceSelector->currentIndex());
	if (space == nullptr)
		return;

	editor::SpeedThresholds taken =
		editor::ThresholdsFromSpeed(space->samples, m_Transport.GetClips());

	if (!taken.refusal.empty())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Thresholds from speed"),
			QString::fromStdString(taken.refusal));
		return;
	}

	space->samples = std::move(taken.run);
	CommitBlendSet();
}

void
AnimationEditorWindow::CreateBlendSet()
{
	if (m_DataRoot.isEmpty() || m_SourceSelector->currentIndex() < 0)
		return;

	const QByteArray animations = m_SourceSelector->currentText().toUtf8();

	try
	{
		const std::string key = editor::CreateEmptyBlendSet(
			std::filesystem::path(m_DataRoot.toStdWString()),
			std::string_view(animations.constData(), static_cast<size_t>(animations.size())));

		// Opening it is a reload: a rig already uploaded refuses a set it was not built with.
		LoadShownMesh(m_SourceSelector->currentText(), QString::fromStdString(key));
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Create Blend Set"),
			QString::fromUtf8(e.what()));
	}
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
	// By name and not by index: an edit that reloads the mesh comes back through here, and the
	// author was watching a clip rather than a row. A name that is gone falls back to the first.
	const int wasClip = m_ClipList->count() > 0 ? m_SelectedClip : -1;
	if (!clips.empty())
	{
		m_ClipList->setCurrentRow(wasClip >= 0 ? wasClip : 0);
		m_FromClip->setCurrentIndex(wasClip >= 0 ? wasClip : 0);
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
	SelectClip(playable ? m_ClipList->currentRow() : -1);
	UpdateTransitionControls();
	SyncTransportUi();
}

void
AnimationEditorWindow::SelectClip(const int index)
{
	m_SelectedClip = index;

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
