#include "BlendSpaceEditorWindow.h"

#include "Render/environment.h"
#include "Windows/AnimationEditor/AnimationPreviewWindow.h"
#include "Windows/AnimationEditor/PlaybackTransport.h"
#include "Windows/AnimationEditor/Scrubber.h"
#include "Windows/AnimationEditor/blend_edits.h"
#include "Windows/AnimationEditor/blend_sets.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "util/asset_paths.h"
#include "util/mime_files.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
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
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTypes>
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/blend.h>
#include <assetlib/codecs.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <gamelib/BlendSpaceInfo.h>
#include <limits>
#include <qcontainerfwd.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qsizepolicy.h>
#include <qstringliteral.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	// What a new sample sits past the run's last, and what a new space's two samples sit at: a whole
	// unit, to be seen and then dragged.
	constexpr float c_ParameterGap = 1.0f;

	// The cursor's resolution: a Scrubber is integer-valued on a closed range.
	constexpr int c_CursorTicks = 1000;

	// How long the pose takes to reach a parameter the cursor jumped to, so a click is not a pop.
	constexpr float c_CursorGlide = 0.08f;

	constexpr int c_ClockIntervalMs = 16;

	// See AnimationEditorWindow::showEvent: the same native viewport in the same tab group.
	constexpr int c_RevealRepaintDelaysMs[] = { 0, 300 };

	[[nodiscard]] std::filesystem::path
	ToPath(const QString& path)
	{
		return std::filesystem::path(path.toStdWString());
	}

	[[nodiscard]] QString
	FirstBlendSet(const QMimeData* mime)
	{
		return editor::FirstLocalFileWithSuffix(mime, assetlib::c_BlendExtension);
	}

	[[nodiscard]] QString
	SampleText(const assetlib::BlendSpaceSample& sample)
	{
		return QStringLiteral("%1    %2")
		    .arg(QString::fromStdString(sample.clip))
		    .arg(sample.parameter, 0, 'f', editor::c_ParameterDecimals);
	}
}

BlendSpaceEditorWindow::BlendSpaceEditorWindow(
	QWidget*                     parent,
	RenderTargetWindowDesc       rt,
	editor::EnvironmentApplyDesc env) : QWidget(parent)
{
	m_Preview = new AnimationPreviewWindow(this, std::move(rt), std::move(env));
	m_Preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_Preview->setMinimumSize(256, 256);

	// The mesh is the set's to decide: one dropped on the viewport would be a rig this set's spaces
	// were never resolved against.
	m_Preview->SetMeshDropsEnabled(false);

	auto* viewportPage   = new QWidget(this);
	auto* viewportLayout = new QVBoxLayout(viewportPage);
	viewportLayout->setContentsMargins(0, 0, 0, 0);
	viewportLayout->setSpacing(0);
	viewportLayout->addWidget(m_Preview, /*stretch*/ 1);
	viewportLayout->addWidget(BuildTransportBar());

	// Pages rather than overlays, for the reason the Animation panel's prompt is one: a label floated
	// over the native surface is at the mercy of its compositing.
	m_ViewNote = new QLabel(this);
	m_ViewNote->setObjectName(QStringLiteral("BlendSpaceViewNote"));
	m_ViewNote->setAlignment(Qt::AlignCenter);
	m_ViewNote->setWordWrap(true);
	m_ViewNote->setEnabled(false);

	m_View = new QStackedWidget(this);
	m_View->addWidget(m_ViewNote);
	m_View->addWidget(viewportPage);

	auto* splitter = new QSplitter(Qt::Horizontal, this);
	splitter->addWidget(BuildPropertiesColumn());
	splitter->addWidget(m_View);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);

	auto* prompt = new QLabel(QStringLiteral("Drop a blend set (.bblend) here"), this);
	prompt->setAlignment(Qt::AlignCenter);
	prompt->setEnabled(false);

	m_Stage = new QStackedWidget(this);
	m_Stage->addWidget(prompt);
	m_Stage->addWidget(splitter);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_Stage);

	connect(m_Preview, &AnimationPreviewWindow::MeshChanged, this, [this](const QString& relPath) {
		m_MeshRelPath = relPath;
		ShowViewPage();
	});
	connect(
		m_Preview,
		&AnimationPreviewWindow::ClipsChanged,
		this,
		&BlendSpaceEditorWindow::SetClips);
	connect(
		m_Preview,
		&AnimationPreviewWindow::SpacesChanged,
		this,
		&BlendSpaceEditorWindow::SetLiveSpaces);

	setAcceptDrops(true);

	m_Clock = new QTimer(this);
	m_Clock->setInterval(c_ClockIntervalMs);
	connect(m_Clock, &QTimer::timeout, this, &BlendSpaceEditorWindow::Tick);

	RefreshSpaces();
}

QWidget*
BlendSpaceEditorWindow::BuildPropertiesColumn()
{
	auto* column = new QWidget(this);
	auto* layout = new QVBoxLayout(column);
	layout->setContentsMargins(4, 4, 4, 4);

	m_SetLabel = new QLabel(column);
	m_SetLabel->setObjectName(QStringLiteral("BlendSetLabel"));
	m_SetLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_SetLabel->setWordWrap(true);

	// The way to let go of the held assets: the explorer refuses to delete or rename what this
	// editor has open, and "close it first" needs a close to point at.
	auto* closeButton = new QPushButton(QStringLiteral("Close"), column);
	connect(closeButton, &QPushButton::clicked, this, &BlendSpaceEditorWindow::CloseBlendSet);

	auto* fileRow = new QHBoxLayout();
	fileRow->setContentsMargins(0, 0, 0, 0);
	fileRow->addWidget(m_SetLabel, /*stretch*/ 1);
	fileRow->addWidget(closeButton);
	layout->addLayout(fileRow);

	m_MeshCaption  = new QLabel(QStringLiteral("Preview Mesh"), column);
	m_MeshSelector = new QComboBox(column);
	m_MeshSelector->setToolTip(QStringLiteral(
		"Every mesh skinned to the rig this set's clip set was resampled against. Which one is "
		"shown is not saved."));
	connect(m_MeshSelector, &QComboBox::activated, this, [this](int index) {
		if (!m_SyncingUi)
			ShowMesh(index);
	});
	m_MeshCaption->hide();
	m_MeshSelector->hide();
	layout->addWidget(m_MeshCaption);
	layout->addWidget(m_MeshSelector);

	layout->addSpacing(8);

	m_SpaceSelector = new QComboBox(column);
	m_SpaceSelector->setObjectName(QStringLiteral("BlendSpaceSelector"));
	m_SpaceSelector->setPlaceholderText(QStringLiteral("no spaces"));
	connect(m_SpaceSelector, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (!m_SyncingUi)
			SelectSpace(index);
	});
	m_AddSpace    = new QPushButton(QStringLiteral("New"), column);
	m_RenameSpace = new QPushButton(QStringLiteral("Rename"), column);
	m_RemoveSpace = new QPushButton(QStringLiteral("Delete"), column);
	m_AddSpace->setObjectName(QStringLiteral("AddBlendSpace"));
	m_RenameSpace->setObjectName(QStringLiteral("RenameBlendSpace"));
	m_RemoveSpace->setObjectName(QStringLiteral("RemoveBlendSpace"));
	connect(m_AddSpace, &QPushButton::clicked, this, &BlendSpaceEditorWindow::AddSpace);
	connect(m_RenameSpace, &QPushButton::clicked, this, &BlendSpaceEditorWindow::RenameSpace);
	connect(m_RemoveSpace, &QPushButton::clicked, this, &BlendSpaceEditorWindow::RemoveSpace);

	auto* spaceRow = new QHBoxLayout();
	spaceRow->addWidget(m_SpaceSelector, /*stretch*/ 1);
	spaceRow->addWidget(m_AddSpace);
	spaceRow->addWidget(m_RenameSpace);
	spaceRow->addWidget(m_RemoveSpace);
	layout->addLayout(spaceRow);

	// Clip and threshold per row, in parameter order -- Unity's Motion list rather than a canvas.
	m_SampleList = new QListWidget(column);
	m_SampleList->setObjectName(QStringLiteral("BlendSpaceSamples"));
	connect(m_SampleList, &QListWidget::currentRowChanged, this, [this](int) {
		if (!m_SyncingUi)
			UpdateSpaceControls();
	});
	layout->addWidget(m_SampleList, /*stretch*/ 1);

	m_SampleClip   = new QComboBox(column);
	m_AddSample    = new QPushButton(QStringLiteral("Add"), column);
	m_RemoveSample = new QPushButton(QStringLiteral("Remove"), column);
	m_AddSample->setObjectName(QStringLiteral("AddBlendSample"));
	m_RemoveSample->setObjectName(QStringLiteral("RemoveBlendSample"));
	connect(m_AddSample, &QPushButton::clicked, this, &BlendSpaceEditorWindow::AddSample);
	connect(m_RemoveSample, &QPushButton::clicked, this, &BlendSpaceEditorWindow::RemoveSample);
	connect(m_SampleClip, &QComboBox::currentIndexChanged, this, [this](int) {
		if (!m_SyncingUi)
			UpdateSpaceControls();
	});

	auto* sampleRow = new QHBoxLayout();
	sampleRow->addWidget(m_SampleClip, /*stretch*/ 1);
	sampleRow->addWidget(m_AddSample);
	sampleRow->addWidget(m_RemoveSample);
	layout->addLayout(sampleRow);

	m_SampleParameter = new QDoubleSpinBox(column);
	m_SampleParameter->setObjectName(QStringLiteral("BlendSampleThreshold"));
	m_SampleParameter->setDecimals(editor::c_ParameterDecimals);
	m_SampleParameter->setSingleStep(editor::c_ParameterStep);
	// Wide enough that no finite parameter is clamped on its way in: a `.bblend` written by hand can
	// carry anything, and a box that clamped would show a number the document does not hold.
	m_SampleParameter->setRange(
		-static_cast<double>(std::numeric_limits<float>::max()),
		static_cast<double>(std::numeric_limits<float>::max()));
	m_SampleParameter->setKeyboardTracking(false);

	// Live while it moves, written when the edit ends: a file rewritten per keystroke is not the
	// point, and a pose that waits for the save is.
	connect(m_SampleParameter, &QDoubleSpinBox::valueChanged, this, [this](double value) {
		if (!m_SyncingUi)
			RetargetSample(static_cast<float>(value));
	});
	connect(m_SampleParameter, &QDoubleSpinBox::editingFinished, this, [this] {
		if (!m_SyncingUi && m_BlendSetDirty)
			CommitBlendSet();
	});

	auto* thresholdRow = new QHBoxLayout();
	thresholdRow->addWidget(new QLabel(QStringLiteral("Threshold"), column));
	thresholdRow->addWidget(m_SampleParameter, /*stretch*/ 1);
	layout->addLayout(thresholdRow);

	m_FromSpeed = new QPushButton(QStringLiteral("Thresholds from speed"), column);
	m_FromSpeed->setObjectName(QStringLiteral("ThresholdsFromSpeed"));
	m_FromSpeed->setToolTip(
		QStringLiteral("Take every threshold from the speed its clip was animated at."));
	connect(m_FromSpeed, &QPushButton::clicked, this, &BlendSpaceEditorWindow::ThresholdsFromSpeed);
	layout->addWidget(m_FromSpeed);

	auto* cursorLine = new QFrame(column);
	cursorLine->setFrameShape(QFrame::HLine);
	cursorLine->setFrameShadow(QFrame::Sunken);
	layout->addWidget(cursorLine);

	m_CursorLabel = new QLabel(column);
	layout->addWidget(m_CursorLabel);

	m_SpaceCursor = new Scrubber(column);
	m_SpaceCursor->SetRange(0, c_CursorTicks);
	connect(m_SpaceCursor, &Scrubber::ValueChanged, this, [this](const int tick) {
		if (!m_SyncingUi)
			MoveCursor(tick);
	});
	layout->addWidget(m_SpaceCursor);

	m_SpaceWeights = new QLabel(column);
	m_SpaceWeights->setWordWrap(true);
	layout->addWidget(m_SpaceWeights);

	m_SpaceNote = new QLabel(column);
	m_SpaceNote->setWordWrap(true);
	m_SpaceNote->setEnabled(false);
	layout->addWidget(m_SpaceNote);

	auto* scrollBox = new QScrollArea(this);
	scrollBox->setWidget(column);
	scrollBox->setWidgetResizable(true);
	scrollBox->setFrameShape(QFrame::NoFrame);
	scrollBox->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scrollBox->setMinimumWidth(column->sizeHint().width());

	// A native surface, beside a native viewport: see docs/known_issues.md, "Scrolling a panel's
	// properties column smears the main tab bar".
	scrollBox->viewport()->setAttribute(Qt::WA_NativeWindow);

	return scrollBox;
}

QWidget*
BlendSpaceEditorWindow::BuildTransportBar()
{
	auto* bar    = new QWidget(this);
	auto* layout = new QHBoxLayout(bar);
	layout->setContentsMargins(4, 4, 4, 4);

	m_PlayButton = new QToolButton(bar);
	m_PlayButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
	connect(m_PlayButton, &QToolButton::clicked, this, [this] { SetPlaying(!m_Playing); });
	layout->addWidget(m_PlayButton);
	layout->addStretch(1);

	// No reverse: a clock running backwards is the one that steps behind a space's reference.
	m_Speed = new QDoubleSpinBox(bar);
	m_Speed->setRange(0.0, 4.0);
	m_Speed->setSingleStep(0.25);
	m_Speed->setValue(1.0);
	m_Speed->setSuffix(QStringLiteral("x"));
	layout->addWidget(m_Speed);

	return bar;
}

void
BlendSpaceEditorWindow::OpenBlendSet(const QString& key)
{
	CloseBlendSet();
	if (m_DataRoot.isEmpty() || key.isEmpty())
		return;

	m_BlendRelPath = key;
	m_SetLabel->setText(key);
	m_Stage->setCurrentIndex(1);

	const std::filesystem::path dataRoot = ToPath(m_DataRoot);
	const std::string           setKey   = key.toStdString();

	try
	{
		m_BlendSet    = editor::LoadBlendSet(dataRoot, setKey);
		m_SetReadable = true;
	}
	catch (const std::exception& e)
	{
		m_ViewReason =
			QStringLiteral("'%1' cannot be read:\n\n%2").arg(key, QString::fromUtf8(e.what()));
		RefreshSpaces();
		ShowViewPage();
		return;
	}

	auto meshes = std::vector<std::string>();
	try
	{
		meshes = editor::ResolveBlendSetMeshes(
			assetlib::AssetRefGraph::Scan(assetlib::AssetStore(dataRoot)),
			setKey);

		if (meshes.empty())
			m_ViewReason = QStringLiteral(
							   "Nothing is skinned to the rig '%1' was resampled against, so there "
							   "is no mesh to show this set on.\n\nIts spaces still edit. Import a "
							   "mesh on that rig to watch them.")
			                   .arg(QString::fromStdString(m_BlendSet.animations));
	}
	catch (const std::exception& e)
	{
		m_ViewReason =
			QStringLiteral("The project cannot be scanned for a mesh to show this set on:\n\n%1")
				.arg(QString::fromUtf8(e.what()));
	}

	m_SyncingUi = true;
	for (const std::string& mesh : meshes) m_MeshSelector->addItem(QString::fromStdString(mesh));
	m_SyncingUi = false;

	m_MeshCaption->setVisible(meshes.size() > 1);
	m_MeshSelector->setVisible(meshes.size() > 1);

	RefreshSpaces();

	if (!meshes.empty())
		ShowMesh(0);

	ShowViewPage();
}

void
BlendSpaceEditorWindow::CloseBlendSet()
{
	// A threshold still being typed is the author's edit, and closing is not a reason to lose it.
	if (m_BlendSetDirty && !m_BlendRelPath.isEmpty() && !m_DataRoot.isEmpty())
	{
		try
		{
			editor::SaveBlendSet(ToPath(m_DataRoot), m_BlendRelPath.toStdString(), m_BlendSet);
		}
		catch (const std::exception& e)
		{
			qWarning(
				"BlendSpaceEditor: an edit to '%s' was not saved: %s",
				qUtf8Printable(m_BlendRelPath),
				e.what());
		}
	}

	m_BlendRelPath.clear();
	m_BlendSet      = assetlib::BlendSet();
	m_SetReadable   = false;
	m_BlendSetDirty = false;
	m_AcquiredSpaces.clear();
	m_SelectedSpace.clear();
	m_PendingSampleRow = -1;
	m_SpaceParameter   = 0.0f;
	m_ViewReason.clear();

	m_Preview->Clear();

	m_SyncingUi = true;
	m_MeshSelector->clear();
	m_SyncingUi = false;
	m_MeshCaption->hide();
	m_MeshSelector->hide();
	m_SetLabel->clear();

	SetPlaying(false);
	m_Stage->setCurrentIndex(0);
	RefreshSpaces();
}

void
BlendSpaceEditorWindow::ShowMesh(const int index)
{
	if (index < 0 || index >= m_MeshSelector->count() || m_DataRoot.isEmpty())
		return;

	m_Preview->LoadMesh(
		ToPath(m_DataRoot) / ToPath(m_MeshSelector->itemText(index)),
		m_BlendSet.animations,
		m_BlendRelPath.toStdString());
}

void
BlendSpaceEditorWindow::ShowViewPage()
{
	if (!m_MeshRelPath.isEmpty())
	{
		m_View->setCurrentIndex(1);
		return;
	}

	m_ViewNote->setText(
		m_ViewReason.isEmpty() ?
			QStringLiteral("The mesh this set is shown on could not be loaded.") :
			m_ViewReason);
	m_View->setCurrentIndex(0);
}

void
BlendSpaceEditorWindow::SetDataRoot(const QString& dataRoot)
{
	m_DataRoot = dataRoot;
	m_Preview->SetDataRoot(ToPath(dataRoot));
	CloseBlendSet();
}

void
BlendSpaceEditorWindow::SetAssets(game::AssetManager* assets)
{
	m_Preview->SetAssets(assets);

	if (assets == nullptr)
		CloseBlendSet();
}

void
BlendSpaceEditorWindow::SetDockVisible(const bool visible)
{
	if (!visible)
		CloseBlendSet();
}

QStringList
BlendSpaceEditorWindow::GetHeldOpenPaths() const
{
	if (m_DataRoot.isEmpty() || m_BlendRelPath.isEmpty())
		return {};

	const QDir root(m_DataRoot);

	auto held = QStringList();
	held << root.absoluteFilePath(m_BlendRelPath);
	if (!m_BlendSet.animations.empty())
		held << root.absoluteFilePath(QString::fromStdString(m_BlendSet.animations));
	if (!m_MeshRelPath.isEmpty())
		held << root.absoluteFilePath(m_MeshRelPath);
	return held;
}

void
BlendSpaceEditorWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (!m_DataRoot.isEmpty() && !FirstBlendSet(event->mimeData()).isEmpty())
		event->acceptProposedAction();
}

void
BlendSpaceEditorWindow::dragMoveEvent(QDragMoveEvent* event)
{
	if (!m_DataRoot.isEmpty() && !FirstBlendSet(event->mimeData()).isEmpty())
		event->acceptProposedAction();
}

void
BlendSpaceEditorWindow::dropEvent(QDropEvent* event)
{
	const QString dropped = FirstBlendSet(event->mimeData());
	const QString key     = editor::GetKeyUnder(m_DataRoot, dropped);
	if (key.isEmpty())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Open Blend Set"),
			QStringLiteral("'%1' is outside the project's Data directory.")
				.arg(QFileInfo(dropped).fileName()));
		return;
	}

	OpenBlendSet(key);
	event->acceptProposedAction();
}

void
BlendSpaceEditorWindow::SetClips(const std::vector<editor::ClipInfo>& clips)
{
	m_Clips   = clips;
	m_Seconds = 0.0f;
	m_Preview->SetTime(m_Seconds);

	SetPlaying(!clips.empty());
	UpdateSpaceControls();
}

void
BlendSpaceEditorWindow::SetLiveSpaces(const std::vector<game::BlendSpaceInfo>& spaces)
{
	m_Spaces = spaces;

	// Only a rig that took the whole document holds it; one that refused the set is compared against
	// nothing, so the next edit rebuilds it rather than writing onto spaces it never had.
	m_AcquiredSpaces.clear();
	if (!m_MeshRelPath.isEmpty() && spaces.size() == m_BlendSet.spaces.size())
		m_AcquiredSpaces = m_BlendSet.spaces;

	RefreshSpaces();
}

void
BlendSpaceEditorWindow::RefreshSpaces()
{
	m_SyncingUi = true;
	m_SpaceSelector->clear();
	for (const assetlib::BlendSpace& space : m_BlendSet.spaces)
		m_SpaceSelector->addItem(QString::fromStdString(space.name));
	m_SpaceSelector->setEnabled(!m_BlendSet.spaces.empty());

	// By name, because an added or removed space moves every index after it.
	const int restored = m_SpaceSelector->findText(m_SelectedSpace);
	m_SyncingUi        = false;

	SelectSpace(m_BlendSet.spaces.empty() ? -1 : std::max(restored, 0));
}

void
BlendSpaceEditorWindow::SelectSpace(const int index)
{
	// Pushed rather than assumed: a combo carrying a placeholder does not select its first item on
	// insert, and everything below reads currentIndex().
	if (m_SpaceSelector->currentIndex() != index)
	{
		const QSignalBlocker blocker(m_SpaceSelector);
		m_SpaceSelector->setCurrentIndex(index);
	}

	m_SampleList->clear();

	const assetlib::BlendSpace* space = EditedSpace(index);
	if (space == nullptr)
	{
		m_SpaceNote->setText(
			m_SetReadable ? QStringLiteral("This set holds no blend spaces yet.") : QString());
		UpdateSpaceControls();
		SyncCursor();
		return;
	}

	m_SelectedSpace = QString::fromStdString(space->name);

	for (const assetlib::BlendSpaceSample& sample : space->samples)
		m_SampleList->addItem(SampleText(sample));

	const int wanted   = m_PendingSampleRow >= 0 ? m_PendingSampleRow : 0;
	m_PendingSampleRow = -1;

	m_SyncingUi = true;
	m_SampleList->setCurrentRow(
		space->samples.empty() ? -1 :
								 std::min(wanted, static_cast<int>(space->samples.size()) - 1));
	m_SyncingUi = false;

	m_SpaceNote->setText(
		space->samples.empty() ?
			QString() :
			QStringLiteral("Parameter %1 to %2")
				.arg(space->samples.front().parameter, 0, 'f', editor::c_ParameterDecimals)
				.arg(space->samples.back().parameter, 0, 'f', editor::c_ParameterDecimals));

	UpdateSpaceControls();
	SyncCursor();
	ShowSelectedSpace();
}

void
BlendSpaceEditorWindow::RelabelSamples()
{
	const assetlib::BlendSpace* space = EditedSpace(m_SpaceSelector->currentIndex());
	if (space == nullptr)
		return;

	for (size_t row = 0; row < space->samples.size(); ++row)
		if (QListWidgetItem* item = m_SampleList->item(static_cast<int>(row)); item != nullptr)
			item->setText(SampleText(space->samples[row]));
}

void
BlendSpaceEditorWindow::UpdateSpaceControls()
{
	assetlib::BlendSpace* space    = EditedSpace(m_SpaceSelector->currentIndex());
	const bool            editable = m_SetReadable;
	const bool            clips    = !m_Clips.empty();

	ShowSampleClips();

	const int looping = LoopingClipCount();
	m_AddSpace->setEnabled(editable && looping >= 2);
	m_AddSpace->setToolTip(
		!editable   ? QString() :
		!clips      ? QStringLiteral(
						  "A new space is seeded from the clip set, which is read from the "
						  "mesh this set is shown on -- and there is none.") :
		looping < 2 ? QStringLiteral("A blend space needs two looping clips; this set has fewer.") :
					  QString());
	m_RenameSpace->setEnabled(editable && space != nullptr);
	m_RemoveSpace->setEnabled(editable && space != nullptr);

	const int  row = m_SampleList->currentRow();
	const bool onSample =
		space != nullptr && row >= 0 && static_cast<size_t>(row) < space->samples.size();

	m_SampleClip->setEnabled(editable && space != nullptr && clips);
	m_AddSample->setEnabled(editable && space != nullptr && m_SampleClip->currentIndex() >= 0);
	m_RemoveSample->setEnabled(editable && onSample && editor::CanRemoveSample(space->samples));
	m_SampleParameter->setEnabled(editable && onSample);
	m_FromSpeed->setEnabled(editable && space != nullptr && clips);

	m_SyncingUi = true;
	if (onSample)
		m_SampleParameter->setValue(
			static_cast<double>(space->samples[static_cast<size_t>(row)].parameter));
	m_SyncingUi = false;
}

void
BlendSpaceEditorWindow::ShowSampleClips()
{
	// Saved and restored rather than set and cleared: this runs inside a sync.
	const bool syncing = m_SyncingUi;
	m_SyncingUi        = true;

	const QString wanted = m_SampleClip->currentText();
	m_SampleClip->clear();

	auto* model = qobject_cast<QStandardItemModel*>(m_SampleClip->model());
	for (const editor::ClipInfo& clip : m_Clips)
	{
		m_SampleClip->addItem(QString::fromStdString(clip.name));

		const std::string_view reason = editor::ClipRefusalReason(clip);
		if (reason.empty() || model == nullptr)
			continue;

		// Listed and disabled rather than dropped: the author is looking for the clip, and its absence
		// would read as a bad clip set rather than as one a blend space cannot hold.
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
BlendSpaceEditorWindow::LoopingClipCount() const
{
	return static_cast<int>(std::ranges::count_if(m_Clips, [](const editor::ClipInfo& clip) {
		return editor::ClipRefusalReason(clip).empty();
	}));
}

int
BlendSpaceEditorWindow::NthLoopingClip(const int n) const
{
	int seen = 0;
	for (size_t i = 0; i < m_Clips.size(); ++i)
	{
		if (!editor::ClipRefusalReason(m_Clips[i]).empty())
			continue;

		if (seen++ == n)
			return static_cast<int>(i);
	}

	return -1;
}

assetlib::BlendSpace*
BlendSpaceEditorWindow::EditedSpace(const int index)
{
	if (index < 0 || static_cast<size_t>(index) >= m_BlendSet.spaces.size())
		return nullptr;

	return &m_BlendSet.spaces[static_cast<size_t>(index)];
}

const game::BlendSpaceInfo*
BlendSpaceEditorWindow::LiveSpace(const int index) const
{
	if (m_MeshRelPath.isEmpty() || m_Spaces.size() != m_BlendSet.spaces.size() || index < 0 ||
	    static_cast<size_t>(index) >= m_Spaces.size())
	{
		return nullptr;
	}

	return &m_Spaces[static_cast<size_t>(index)];
}

QString
BlendSpaceEditorWindow::ClipName(const uint32_t clipIndex) const
{
	return clipIndex < m_Clips.size() ? QString::fromStdString(m_Clips[clipIndex].name) :
	                                    QStringLiteral("<clip %1>").arg(clipIndex);
}

void
BlendSpaceEditorWindow::CommitBlendSet()
{
	if (m_BlendRelPath.isEmpty() || m_DataRoot.isEmpty())
		return;

	try
	{
		editor::SaveBlendSet(ToPath(m_DataRoot), m_BlendRelPath.toStdString(), m_BlendSet);
	}
	catch (const std::exception& e)
	{
		// The edit stays on screen: it is the author's, and reverting it would throw away the work
		// rather than the mistake.
		QMessageBox::warning(window(), QStringLiteral("Blend Set"), QString::fromUtf8(e.what()));
		return;
	}

	m_BlendSetDirty = false;

	if (m_MeshRelPath.isEmpty())
	{
		RefreshSpaces();
		return;
	}

	// Anything but a threshold that moved is a node table that has to be built again.
	if (!editor::IsParameterMove(m_AcquiredSpaces, m_BlendSet.spaces))
	{
		ShowMesh(m_MeshSelector->currentIndex());
		return;
	}

	RelabelSamples();

	if (!editor::ApplyParameters(m_BlendSet.spaces, m_Spaces))
		return;

	const QString refusal = m_Preview->RetargetBlendParameters(m_Spaces);
	if (!refusal.isEmpty())
		qWarning("BlendSpaceEditor: a threshold did not go live: %s", qUtf8Printable(refusal));

	SyncCursor();
}

void
BlendSpaceEditorWindow::AddSpace()
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

	// Two samples, because `validateBlendSet` refuses a shorter run: there is no empty space to add
	// and fill in. The first two looping clips at 0 and 1 are a run to edit, not a guess at intent.
	auto space = assetlib::BlendSpace();
	space.name = wanted;
	for (int n = 0; n < 2; ++n)
	{
		const int clip = NthLoopingClip(n);
		if (clip < 0)
			return;

		space.samples.emplace_back(
			m_Clips[static_cast<size_t>(clip)].name,
			static_cast<float>(n) * c_ParameterGap);
	}

	m_SelectedSpace = QString::fromStdString(space.name);
	m_BlendSet.spaces.push_back(std::move(space));
	CommitBlendSet();
}

void
BlendSpaceEditorWindow::RemoveSpace()
{
	const int index = m_SpaceSelector->currentIndex();
	if (EditedSpace(index) == nullptr)
		return;

	m_BlendSet.spaces.erase(m_BlendSet.spaces.begin() + static_cast<ptrdiff_t>(index));
	CommitBlendSet();
}

void
BlendSpaceEditorWindow::RenameSpace()
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

	space->name     = wanted;
	m_SelectedSpace = QString::fromStdString(wanted);
	CommitBlendSet();
}

void
BlendSpaceEditorWindow::AddSample()
{
	assetlib::BlendSpace* space = EditedSpace(m_SpaceSelector->currentIndex());
	const int             index = m_SampleClip->currentIndex();
	if (space == nullptr || index < 0 || static_cast<size_t>(index) >= m_Clips.size())
		return;

	const editor::ClipInfo& clip = m_Clips[static_cast<size_t>(index)];

	// The combo lists a one-shot disabled rather than hiding it, and a selection restored by name can
	// come back on a clip that no longer loops, so the refusal is still owed here.
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

	const float parameter =
		space->samples.empty() ? 0.0f : space->samples.back().parameter + c_ParameterGap;

	if (!editor::CanInsertAt(space->samples, parameter, editor::c_ParameterStep))
	{
		// Reachable off a run authored by hand, where one gap past a large last parameter is the same
		// number again.
		QMessageBox::warning(
			window(),
			QStringLiteral("Add Sample"),
			QStringLiteral("There is no room past the last sample for another threshold."));
		return;
	}

	const size_t at = editor::InsertionIndex(space->samples, parameter);

	m_PendingSampleRow = static_cast<int>(at);
	space->samples.insert(
		space->samples.begin() + static_cast<ptrdiff_t>(at),
		{ clip.name, parameter });

	CommitBlendSet();
}

void
BlendSpaceEditorWindow::RemoveSample()
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
BlendSpaceEditorWindow::RetargetSample(const float parameter)
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
		editor::c_ParameterStep);

	space->samples[static_cast<size_t>(row)].parameter = held;
	m_BlendSetDirty                                    = true;

	if (LiveSpace(m_SpaceSelector->currentIndex()) != nullptr &&
	    editor::ApplyParameters(m_BlendSet.spaces, m_Spaces))
	{
		const QString refusal = m_Preview->RetargetBlendParameters(m_Spaces);
		if (!refusal.isEmpty())
			qWarning("BlendSpaceEditor: a threshold did not go live: %s", qUtf8Printable(refusal));
	}

	// The run's extent may have moved with it, and the weights are a different pair of clips once a
	// neighbour crosses the cursor.
	SyncCursor();

	m_SyncingUi = true;
	if (QListWidgetItem* item = m_SampleList->item(row); item != nullptr)
		item->setText(SampleText(space->samples[static_cast<size_t>(row)]));
	m_SampleParameter->setValue(static_cast<double>(held));
	m_SyncingUi = false;
}

void
BlendSpaceEditorWindow::ShowSelectedSpace()
{
	const int index = m_SpaceSelector->currentIndex();
	if (LiveSpace(index) == nullptr)
		return;

	m_Preview->ShowSpace(static_cast<uint32_t>(index), m_SpaceParameter, m_Seconds);
	ShowCursorWeights();
}

void
BlendSpaceEditorWindow::MoveCursor(const int tick)
{
	const int                   index = m_SpaceSelector->currentIndex();
	const game::BlendSpaceInfo* space = LiveSpace(index);
	if (space == nullptr)
		return;

	m_SpaceParameter =
		editor::ParameterForTick(space->ParameterMin(), space->ParameterMax(), c_CursorTicks, tick);

	// Retargeted rather than restamped: the slot's phase is rebased onto now first, so the pose keeps
	// the cycle it was already walking instead of jumping on the frame of the write.
	m_Preview
		->RetargetSpace(static_cast<uint32_t>(index), m_SpaceParameter, m_Seconds, c_CursorGlide);

	ShowCursorWeights();
}

void
BlendSpaceEditorWindow::SyncCursor()
{
	const game::BlendSpaceInfo* space = LiveSpace(m_SpaceSelector->currentIndex());
	m_SpaceCursor->setEnabled(space != nullptr);

	if (space == nullptr)
	{
		m_CursorLabel->clear();
		m_SpaceWeights->clear();
		return;
	}

	// Held inside the run the cursor now addresses: a space selected after another one leaves the
	// parameter somewhere its range may not reach.
	m_SpaceParameter = std::clamp(m_SpaceParameter, space->ParameterMin(), space->ParameterMax());

	m_SyncingUi = true;
	m_SpaceCursor->SetValue(
		editor::TickForParameter(
			space->ParameterMin(),
			space->ParameterMax(),
			c_CursorTicks,
			m_SpaceParameter));
	m_SyncingUi = false;

	ShowCursorWeights();
}

void
BlendSpaceEditorWindow::ShowCursorWeights()
{
	const game::BlendSpaceInfo* space = LiveSpace(m_SpaceSelector->currentIndex());
	if (space == nullptr)
		return;

	const game::BlendSpaceStraddle at = space->StraddleAt(m_SpaceParameter);

	m_CursorLabel->setText(
		QStringLiteral("Parameter %1").arg(m_SpaceParameter, 0, 'f', editor::c_ParameterDecimals));

	const auto clipName = [this, space](const size_t sample) {
		return ClipName(space->samples[sample].clipIndex);
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
BlendSpaceEditorWindow::ThresholdsFromSpeed()
{
	assetlib::BlendSpace* space = EditedSpace(m_SpaceSelector->currentIndex());
	if (space == nullptr)
		return;

	editor::SpeedThresholds taken = editor::ThresholdsFromSpeed(space->samples, m_Clips);

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
BlendSpaceEditorWindow::SetPlaying(const bool playing)
{
	m_Playing = playing;
	m_PlayButton->setIcon(
		style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));

	if (playing && isVisible())
	{
		m_ClockDelta.restart();
		m_Clock->start();
	}
	else
	{
		m_Clock->stop();
	}
}

void
BlendSpaceEditorWindow::Tick()
{
	const float dt = static_cast<float>(m_ClockDelta.restart()) / 1000.0f;
	m_Seconds += dt * static_cast<float>(m_Speed->value());
	m_Preview->SetTime(m_Seconds);
}

void
BlendSpaceEditorWindow::hideEvent(QHideEvent* event)
{
	QWidget::hideEvent(event);
	m_Clock->stop();
}

void
BlendSpaceEditorWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);

	for (const int delayMs : c_RevealRepaintDelaysMs)
		QTimer::singleShot(delayMs, this, [this] { window()->update(); });

	if (m_Playing)
	{
		m_ClockDelta.restart();
		m_Clock->start();
	}
}
