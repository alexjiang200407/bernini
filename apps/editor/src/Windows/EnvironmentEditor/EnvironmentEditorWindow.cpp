#include "EnvironmentEditorWindow.h"

#include "Windows/EnvironmentEditor/EnvironmentPreviewWindow.h"
#include "Windows/EnvironmentEditor/environment_editor_ui.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <assetlib/AssetStore.h>

namespace
{
	QString
	Displayed(const std::string& reference)
	{
		return reference.empty() ? QStringLiteral("(none)") : QString::fromStdString(reference);
	}
}

EnvironmentEditorWindow::EnvironmentEditorWindow(
	QWidget*                    parent,
	EnvironmentEditorWindowDesc desc) : QWidget(parent), m_Desc(std::move(desc))
{
	auto* splitter = new QSplitter(Qt::Horizontal, this);

	const editor::EnvironmentEditorWidgets ui = editor::BuildEnvironmentEditorUi(splitter);

	m_Open             = ui.open;
	m_Save             = ui.save;
	m_EnvironmentLabel = ui.environmentLabel;
	m_CompositionLabel = ui.compositionLabel;
	m_SkyMipLevel      = ui.skyMipLevel;
	m_SkyRotationY     = ui.skyRotationY;
	m_OverrideExposure = ui.overrideExposure;
	m_Exposure         = ui.exposure;
	m_RimTint          = ui.rimTint;
	m_RimIntensity     = ui.rimIntensity;
	m_RimPower         = ui.rimPower;

	m_DataRoot = m_Desc.startupEnv.dataRoot;

	connect(m_Open, &QPushButton::clicked, this, [this]() {
		const QString path = QFileDialog::getOpenFileName(
			window(),
			QStringLiteral("Open Environment"),
			QString::fromStdWString(m_DataRoot.wstring()),
			QStringLiteral("Bernini Environment (*.benv)"));
		if (!path.isEmpty())
			OpenEnvironment(path);
	});

	connect(m_Save, &QPushButton::clicked, this, &EnvironmentEditorWindow::SaveEnvironment);
	connect(m_RimTint, &QPushButton::clicked, this, &EnvironmentEditorWindow::PickTint);

	const auto onEdit = [this]() { ApplyControls(); };
	connect(m_SkyMipLevel, &QSpinBox::valueChanged, this, onEdit);
	connect(m_SkyRotationY, &QDoubleSpinBox::valueChanged, this, onEdit);
	connect(m_RimIntensity, &QDoubleSpinBox::valueChanged, this, onEdit);
	connect(m_RimPower, &QDoubleSpinBox::valueChanged, this, onEdit);
	connect(m_Exposure, &QDoubleSpinBox::valueChanged, this, onEdit);
	connect(m_OverrideExposure, &QCheckBox::toggled, this, onEdit);

	QWidget* right = nullptr;
	if (m_Desc.renderer != nullptr)
	{
		auto rtDesc             = RenderTargetWindowDesc();
		rtDesc.renderer         = m_Desc.renderer;
		rtDesc.taaEnabled       = m_Desc.taaEnabled;
		rtDesc.renderScale      = m_Desc.renderScale;
		rtDesc.initialInstances = 8;

		m_Preview = new EnvironmentPreviewWindow(splitter, std::move(rtDesc));
		right     = m_Preview;

		connect(
			m_Preview,
			&EnvironmentPreviewWindow::EnvironmentDropped,
			this,
			&EnvironmentEditorWindow::OpenEnvironment);
	}
	else
	{
		auto* placeholder = new QLabel("No graphics device", splitter);
		placeholder->setAlignment(Qt::AlignCenter);
		placeholder->setStyleSheet("color: gray;");
		right = placeholder;
	}

	splitter->addWidget(ui.panel);
	splitter->addWidget(right);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({ 280, 900 });

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(splitter);

	// Not resolved against the data root: config.json's environmentMap is a path in its own right,
	// the way every other viewport takes it, and OpenEnvironment is what makes it a mount key.
	if (!m_Desc.startupEnv.environmentMap.empty())
		OpenEnvironment(QString::fromStdString(m_Desc.startupEnv.environmentMap));
}

EnvironmentEditorWindow::~EnvironmentEditorWindow()
{
	if (m_Preview != nullptr)
		m_Preview->Release();
}

void
EnvironmentEditorWindow::SetDataRoot(const QString& dataRoot)
{
	m_DataRoot = std::filesystem::path(dataRoot.toStdWString());
	Reset();
}

QStringList
EnvironmentEditorWindow::GetHeldOpenPaths() const
{
	return m_EnvPath.isEmpty() ? QStringList() : QStringList{ m_EnvPath };
}

void
EnvironmentEditorWindow::Reset()
{
	m_Env = assetlib::BEnv();
	m_EnvPath.clear();
	m_Dirty = false;

	SyncControls();
}

void
EnvironmentEditorWindow::OpenEnvironment(const QString& path)
{
	const std::filesystem::path file(path.toStdWString());

	try
	{
		const assetlib::AssetStore store(m_DataRoot);
		m_Env = store.Load<assetlib::BEnv>(store.KeyFor(file));
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Open Environment"),
			QStringLiteral("'%1' could not be read:\n%2")
				.arg(path)
				.arg(QString::fromLatin1(e.what())));
		return;
	}

	m_EnvPath = path;
	m_Dirty   = false;

	if (m_Preview != nullptr)
	{
		m_Preview->Bind(path.toStdString(), m_DataRoot);
		m_Preview->ApplyEdits(m_Env);
	}

	SyncControls();
}

void
EnvironmentEditorWindow::SyncControls()
{
	const bool open = !m_EnvPath.isEmpty();

	m_Syncing = true;

	m_SkyMipLevel->setValue(static_cast<int>(m_Env.skyMipLevel));
	m_SkyRotationY->setValue(glm::degrees(m_Env.skyRotationY));

	m_OverrideExposure->setChecked(m_Env.pbr.exposureOverride.has_value());
	m_Exposure->setValue(m_Env.pbr.exposureOverride.value_or(
		m_Preview != nullptr ? m_Preview->GetDerivedExposure() : 1.0f));

	editor::SetTintSwatch(m_RimTint, m_Env.rim.tint);
	m_RimIntensity->setValue(m_Env.rim.intensity);
	m_RimPower->setValue(m_Env.rim.power);

	m_Syncing = false;

	m_EnvironmentLabel->setText(open ? m_EnvPath : QStringLiteral("(no environment open)"));
	m_EnvironmentLabel->setToolTip(m_EnvPath);
	m_CompositionLabel->setText(
		open ? QStringLiteral("Sky: %1\nLighting: %2")
				   .arg(Displayed(m_Env.sky))
				   .arg(Displayed(m_Env.pbr.lighting)) :
			   QString());

	const std::array<QWidget*, 6> controls = { m_SkyMipLevel, m_SkyRotationY, m_OverrideExposure,
		                                       m_RimTint,     m_RimIntensity, m_RimPower };
	for (QWidget* control : controls) control->setEnabled(open);
	m_Exposure->setEnabled(open && m_OverrideExposure->isChecked());
	m_Save->setEnabled(open && m_Dirty);
}

void
EnvironmentEditorWindow::ApplyControls()
{
	if (m_Syncing || m_EnvPath.isEmpty())
		return;

	m_Env.skyMipLevel  = static_cast<uint32_t>(m_SkyMipLevel->value());
	m_Env.skyRotationY = glm::radians(static_cast<float>(m_SkyRotationY->value()));

	m_Env.pbr.exposureOverride = m_OverrideExposure->isChecked() ?
	                                 std::optional<float>(static_cast<float>(m_Exposure->value())) :
	                                 std::nullopt;

	m_Env.rim.intensity = static_cast<float>(m_RimIntensity->value());
	m_Env.rim.power     = static_cast<float>(m_RimPower->value());

	m_Exposure->setEnabled(m_OverrideExposure->isChecked());

	m_Dirty = true;
	m_Save->setEnabled(true);

	if (m_Preview != nullptr)
		m_Preview->ApplyEdits(m_Env);
}

void
EnvironmentEditorWindow::PickTint()
{
	const auto channel = [](float v) {
		return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
	};

	const QColor picked = QColorDialog::getColor(
		QColor(channel(m_Env.rim.tint.r), channel(m_Env.rim.tint.g), channel(m_Env.rim.tint.b)),
		window(),
		QStringLiteral("Rim Tint"));

	if (!picked.isValid())
		return;

	m_Env.rim.tint = glm::vec3(
		static_cast<float>(picked.redF()),
		static_cast<float>(picked.greenF()),
		static_cast<float>(picked.blueF()));

	editor::SetTintSwatch(m_RimTint, m_Env.rim.tint);

	// Not through ApplyControls: the tint has no control to read it back out of.
	m_Dirty = true;
	m_Save->setEnabled(true);

	if (m_Preview != nullptr)
		m_Preview->ApplyEdits(m_Env);
}

void
EnvironmentEditorWindow::SaveEnvironment()
{
	if (m_EnvPath.isEmpty())
		return;

	try
	{
		const assetlib::AssetStore store(m_DataRoot);
		store.Save(m_Env, store.KeyFor(std::filesystem::path(m_EnvPath.toStdWString())));
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Save Environment"),
			QStringLiteral("'%1' could not be written:\n%2")
				.arg(m_EnvPath)
				.arg(QString::fromLatin1(e.what())));
		return;
	}

	m_Dirty = false;
	m_Save->setEnabled(false);
}
