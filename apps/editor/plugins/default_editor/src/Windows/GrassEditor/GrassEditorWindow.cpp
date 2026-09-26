#include "GrassEditorWindow.h"
#include <bgl/Camera.h>
#include <editor_plugin_api/EditorPanel.h>

#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_plugin_api/localize.h>
#include <editor_sdk/environment.h>

#include <QColor>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/grass_patch.h>
#include <assetlib_structs/BGrass.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/WindDesc.h>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <gamelib/AssetManager.h>
#include <optional>
#include <qlogging.h>
#include <qnamespace.h>
#include <qstringliteral.h>
#include <qwidget.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	// The patch is at most this wide, whatever the fade: a look fading out at 60 m still shows its
	// near field and its thinning, and a 120 m patch at preview spacing is a quarter million clumps.
	constexpr float c_MaxPatchSize = 60.0f;
	constexpr float c_MinPatchSize = 4.0f;

	// About 40,000 clumps however wide the patch, spaced no tighter than a verge's.
	constexpr float c_PatchClumpsAcross = 200.0f;
	constexpr float c_MinPatchSpacing   = 0.25f;

	// Where the eye starts: standing at the patch's edge of the near field, looking along it.
	constexpr float c_StartRadius = 3.0f;
	constexpr float c_StartPitch  = 0.25f;

	// A late-morning sun, so a look's translucency has something behind it to show.
	constexpr float c_SunAzimuth   = 0.6f;
	constexpr float c_SunElevation = 0.66f;
	constexpr float c_SunIntensity = 2.0f;

	constexpr int c_ClockIntervalMs = 16;

	// Tints are linear multipliers; a swatch shows them as the display would.
	QColor
	SwatchOf(const glm::vec3& linear)
	{
		const auto encode = [](float v) {
			return std::clamp(std::pow(std::max(v, 0.0f), 1.0f / 2.2f), 0.0f, 1.0f);
		};
		return QColor::fromRgbF(encode(linear.r), encode(linear.g), encode(linear.b));
	}

	glm::vec3
	LinearOf(const QColor& swatch)
	{
		const auto decode = [](float v) { return std::pow(v, 2.2f); };
		return glm::vec3(
			decode(static_cast<float>(swatch.redF())),
			decode(static_cast<float>(swatch.greenF())),
			decode(static_cast<float>(swatch.blueF())));
	}

	void
	Paint(QToolButton* button, const glm::vec3& linear)
	{
		button->setStyleSheet(
			QStringLiteral("QToolButton { background-color: %1; min-width: 48px; }")
				.arg(SwatchOf(linear).name()));
	}
}

namespace editor
{
	assetlib::GrassPatchDesc
	PreviewPatchFor(const assetlib::BGrass& look) noexcept
	{
		auto patch = assetlib::GrassPatchDesc();
		patch.size = std::clamp(2.0f * look.density.fadeEnd, c_MinPatchSize, c_MaxPatchSize);
		if (!std::isfinite(patch.size))
			patch.size = c_MinPatchSize;
		patch.spacing = std::max(c_MinPatchSpacing, patch.size / c_PatchClumpsAcross);
		return patch;
	}

	bgl::WindDesc
	PreviewWind(
		float strength,
		float heading,
		float gustStrength,
		float gustScale,
		float gustSpeed) noexcept
	{
		const float radians = glm::radians(heading);

		auto wind         = bgl::WindDesc();
		wind.direction    = glm::vec3(std::cos(radians), 0.0f, std::sin(radians));
		wind.strength     = strength;
		wind.gustStrength = gustStrength;
		wind.gustScale    = gustScale;
		wind.gustSpeed    = gustSpeed;
		return wind;
	}
}

GrassEditorWindow::GrassEditorWindow(
	editor::IEditorHost&         host,
	QWidget*                     parent,
	editor::ViewportDesc         rt,
	editor::EnvironmentApplyDesc env) : editor::AssetEditorPanel(parent), m_Host(host)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	m_Stage = new QStackedWidget(this);
	layout->addWidget(m_Stage);

	auto* prompt = new QLabel(
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.empty_prompt",
			"Open a .bgrass from the Content Explorer to edit the look and watch it grow."),
		m_Stage);
	prompt->setAlignment(Qt::AlignCenter);
	prompt->setWordWrap(true);
	m_Stage->addWidget(prompt);

	auto* split = new QSplitter(Qt::Horizontal, m_Stage);
	split->addWidget(BuildColumn());

	auto* view       = new QWidget(split);
	auto* viewLayout = new QVBoxLayout(view);
	viewLayout->setContentsMargins(0, 0, 0, 0);
	m_Viewport = m_Host.CreateViewport(view, rt);
	m_Viewport->installEventFilter(this);
	viewLayout->addWidget(m_Viewport, /*stretch*/ 1);
	viewLayout->addWidget(BuildWindStrip());
	split->addWidget(view);
	split->setStretchFactor(1, 1);
	m_Stage->addWidget(split);

	// Grass sways only while its clock runs, so a still preview is a calm one whatever the strip says.
	m_Clock = new QTimer(this);
	m_Clock->setInterval(c_ClockIntervalMs);
	connect(m_Clock, &QTimer::timeout, this, &GrassEditorWindow::Tick);
	m_Elapsed.start();

	// A world rather than a swatch: the sky as it is, and fixed while the camera turns.
	m_Environment.configured                 = std::move(env);
	m_Environment.configured.sky.mipLevel    = std::nullopt;
	m_Environment.configured.sky.opacity     = 1.0f;
	m_Environment.configured.sky.followsView = false;

	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		if (!m_Environment.configured.environmentMap.empty())
		{
			try
			{
				auto external = std::optional<assetlib::AssetStore>();
				if (!m_Environment.configured.dataRoot.empty() &&
				    m_Environment.configured.dataRoot != m_Host.GetStore().GetDataRoot())
					external.emplace(m_Environment.configured.dataRoot);

				editor::BindEnvironment(
					&context.scene,
					view.Get(),
					m_Environment,
					m_Environment.configured.environmentMap,
					external ? *external : m_Host.GetStore(),
					"GrassEditor");
			}
			catch (const std::exception& error)
			{
				qWarning(
					"GrassEditor: the configured environment could not be loaded: %s",
					error.what());
			}
		}

		const float azimuth   = c_SunAzimuth;
		const float elevation = c_SunElevation;
		view->SetDirectionalLight(
			{ .direction = -glm::vec3(
				  std::cos(elevation) * std::sin(azimuth),
				  std::sin(elevation),
				  std::cos(elevation) * std::cos(azimuth)),
		      .color     = glm::vec3(1.0f, 0.96f, 0.88f),
		      .intensity = c_SunIntensity });

		m_Ground = context.scene.CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(0.22f, 0.19f, 0.15f, 1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 1.0f });
	});

	m_Orbit.FocusOn(glm::vec3(0.0f, 0.3f, 0.0f), c_StartRadius, 0.0f, c_StartPitch);
	PushWind();
	UpdateTitle();
}

GrassEditorWindow::~GrassEditorWindow()
{
	m_Clock->stop();
	m_Viewport->SetRenderingEnabled(false);
	ReleasePreview();
	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef&) {
		try
		{
			editor::ReleaseEnvironment(&context.scene, m_Environment);
			if (m_Ground.IsValid())
				context.scene.DeleteMaterial(m_Ground);
		}
		catch (const std::exception& error)
		{
			qWarning("GrassEditor: failed to release preview resources: %s", error.what());
		}
	});
}

QWidget*
GrassEditorWindow::BuildColumn()
{
	auto* scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setMinimumWidth(320);

	auto* column = new QWidget(scroll);
	auto* layout = new QVBoxLayout(column);

	m_Title = new QLabel(column);
	m_Title->setObjectName(QStringLiteral("GrassTitle"));
	m_Title->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_Title->setWordWrap(true);
	layout->addWidget(m_Title);

	m_Save = new QPushButton(
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.save_button", "Save"),
		column);
	m_Save->setObjectName(QStringLiteral("GrassSave"));
	connect(m_Save, &QPushButton::clicked, this, [this] { Save(); });

	m_Revert = new QPushButton(
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.revert_button", "Revert"),
		column);
	m_Revert->setObjectName(QStringLiteral("GrassRevert"));
	m_Revert->setToolTip(
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.revert_tooltip",
			"Put back what the file says, dropping every unsaved edit."));
	connect(m_Revert, &QPushButton::clicked, this, &GrassEditorWindow::Revert);

	auto* buttons = new QHBoxLayout();
	buttons->addWidget(m_Save);
	buttons->addWidget(m_Revert);
	buttons->addStretch(1);
	layout->addLayout(buttons);

	m_Status = new QLabel(column);
	m_Status->setObjectName(QStringLiteral("GrassStatus"));
	m_Status->setWordWrap(true);
	m_Status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_Status->hide();
	layout->addWidget(m_Status);

	const auto group = [&](const QString& title) {
		auto* box  = new QGroupBox(title, column);
		auto* form = new QFormLayout(box);
		layout->addWidget(box);
		return form;
	};

	QFormLayout* surface = group(
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.surface_group", "Surface"));
	m_Material = new QLineEdit(column);
	m_Material->setObjectName(QStringLiteral("GrassMaterial"));
	m_Material->setPlaceholderText(
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.material_placeholder",
			"Authored/Materials/….bmaterial"));
	m_Material->setToolTip(
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.material_tooltip",
			"The .bmaterial every blade shades through, as a key under the data root. Its alpha is "
			"never tested: a blade is solid geometry."));
	connect(m_Material, &QLineEdit::editingFinished, this, [this] {
		const std::string material = m_Material->text().trimmed().toStdString();
		if (m_Syncing || material == m_Look.material)
			return;
		m_Look.material = material;
		Edited(false);
	});
	surface->addRow(
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.material_label", "Material"),
		m_Material);

	using Look = assetlib::BGrass;

	QFormLayout* blade =
		group(editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.blade_group", "Blade"));
	AddReal(
		blade,
		"min_height",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.min_height", "Min height"),
		0.01,
		5.0,
		0.05,
		[](Look& l) -> float& { return l.blade.minHeight; });
	AddReal(
		blade,
		"max_height",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.max_height", "Max height"),
		0.01,
		5.0,
		0.05,
		[](Look& l) -> float& { return l.blade.maxHeight; });
	AddReal(
		blade,
		"root_width",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.root_width", "Root width"),
		0.001,
		0.5,
		0.005,
		[](Look& l) -> float& { return l.blade.rootWidth; });
	AddReal(
		blade,
		"tip_width",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.tip_width", "Tip width"),
		0.0,
		1.0,
		0.05,
		[](Look& l) -> float& { return l.blade.tipWidth; });
	AddReal(
		blade,
		"curvature",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.curvature", "Curvature"),
		0.0,
		1.0,
		0.05,
		[](Look& l) -> float& { return l.blade.curvature; });
	AddReal(
		blade,
		"lean",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.lean", "Lean"),
		0.0,
		1.0,
		0.05,
		[](Look& l) -> float& { return l.blade.lean; });
	AddCount(
		blade,
		"near_segments",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.near_segments",
			"Segments near"),
		1,
		7,
		[](Look& l) -> uint32_t& { return l.blade.nearSegments; });
	AddCount(
		blade,
		"far_segments",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.far_segments",
			"Segments far"),
		1,
		7,
		[](Look& l) -> uint32_t& { return l.blade.farSegments; });

	QFormLayout* clump =
		group(editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.clump_group", "Clump"));
	AddCount(
		clump,
		"blades_per_clump",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.blades_per_clump",
			"Blades per clump"),
		1,
		16,
		[](Look& l) -> uint32_t& { return l.clump.bladesPerClump; });
	AddReal(
		clump,
		"radius",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.radius", "Radius"),
		0.0,
		2.0,
		0.01,
		[](Look& l) -> float& { return l.clump.radius; });

	QFormLayout* density = group(
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.density_group", "Distance"));
	AddReal(
		density,
		"fade_start",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.fade_start", "Fade start"),
		0.0,
		500.0,
		1.0,
		[](Look& l) -> float& { return l.density.fadeStart; });
	AddReal(
		density,
		"fade_end",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.fade_end", "Fade end"),
		0.1,
		500.0,
		1.0,
		[](Look& l) -> float& { return l.density.fadeEnd; },
		/*resizesPatch*/ true);
	AddReal(
		density,
		"widening",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.widening", "Widening"),
		0.0,
		4.0,
		0.1,
		[](Look& l) -> float& { return l.density.widening; });

	QFormLayout* response = group(
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.response_group",
			"Wind response"));
	AddReal(
		response,
		"stiffness",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.stiffness", "Stiffness"),
		0.0,
		1.0,
		0.05,
		[](Look& l) -> float& { return l.response.stiffness; });
	AddReal(
		response,
		"gust_response",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.gust_response",
			"Gust response"),
		0.0,
		4.0,
		0.1,
		[](Look& l) -> float& { return l.response.gustResponse; });

	QFormLayout* lighting = group(
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.lighting_group", "Lighting"));
	AddReal(
		lighting,
		"root_occlusion",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.root_occlusion",
			"Root occlusion"),
		0.0,
		1.0,
		0.05,
		[](Look& l) -> float& { return l.lighting.rootOcclusion; });
	AddReal(
		lighting,
		"normal_rounding",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.normal_rounding",
			"Normal rounding"),
		0.0,
		1.0,
		0.05,
		[](Look& l) -> float& { return l.lighting.normalRounding; });
	AddReal(
		lighting,
		"ground_normal_near",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.ground_normal_near",
			"Ground normal near"),
		0.0,
		1.0,
		0.05,
		[](Look& l) -> float& { return l.lighting.groundNormalNear; });
	AddReal(
		lighting,
		"ground_normal_far",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.ground_normal_far",
			"Ground normal far"),
		0.0,
		1.0,
		0.05,
		[](Look& l) -> float& { return l.lighting.groundNormalFar; });
	AddReal(
		lighting,
		"translucency",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.translucency",
			"Translucency"),
		0.0,
		4.0,
		0.05,
		[](Look& l) -> float& { return l.lighting.translucency; });
	AddColour(
		lighting,
		"translucency_color",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.translucency_color",
			"Translucency colour"),
		[](Look& l) -> glm::vec3& { return l.lighting.translucencyColor; });

	QFormLayout* colour = group(
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.color_group", "Colour"));
	AddColour(
		colour,
		"root_tint",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.root_tint", "Root tint"),
		[](Look& l) -> glm::vec3& { return l.color.rootTint; });
	AddColour(
		colour,
		"tip_tint",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.tip_tint", "Tip tint"),
		[](Look& l) -> glm::vec3& { return l.color.tipTint; });
	AddReal(
		colour,
		"variation",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.variation", "Variation"),
		0.0,
		1.0,
		0.05,
		[](Look& l) -> float& { return l.color.variation; });

	layout->addStretch(1);
	scroll->setWidget(column);
	return scroll;
}

QWidget*
GrassEditorWindow::BuildWindStrip()
{
	auto* strip  = new QWidget(this);
	auto* layout = new QHBoxLayout(strip);
	layout->setContentsMargins(6, 4, 6, 4);

	const auto spin =
		[&](const char* name, const QString& label, double max, double step, double value) {
			auto* box = new QDoubleSpinBox(strip);
			box->setObjectName(QString::fromLatin1(name));
			box->setRange(0.0, max);
			box->setSingleStep(step);
			box->setDecimals(2);
			box->setValue(value);
			connect(box, &QDoubleSpinBox::valueChanged, this, &GrassEditorWindow::PushWind);
			layout->addWidget(new QLabel(label, strip));
			layout->addWidget(box);
			return box;
		};

	const auto defaults = bgl::WindDesc();
	m_WindStrength      = spin(
		"GrassWindStrength",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.wind_strength", "Wind"),
		1.0,
		0.05,
		0.2);
	m_WindHeading = spin(
		"GrassWindHeading",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.wind_heading", "Heading"),
		360.0,
		15.0,
		0.0);
	m_WindGustStrength = spin(
		"GrassWindGusts",
		editor::Localize(m_Host.GetLanguageResolver(), "bernini.grass.wind_gusts", "Gusts"),
		1.0,
		0.05,
		0.3);
	m_WindGustScale = spin(
		"GrassWindGustScale",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.wind_gust_scale",
			"Gust size"),
		100.0,
		1.0,
		defaults.gustScale);
	m_WindGustSpeed = spin(
		"GrassWindGustSpeed",
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.wind_gust_speed",
			"Gust speed"),
		50.0,
		0.5,
		defaults.gustSpeed);
	m_WindGustScale->setMinimum(0.1);
	m_WindHeading->setWrapping(true);
	layout->addStretch(1);

	strip->setToolTip(
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.grass.wind_tooltip",
			"The preview's wind. It is not part of the look and is never saved: a game sets its "
			"own."));
	return strip;
}

void
GrassEditorWindow::AddReal(
	QFormLayout*                                    form,
	const char*                                     key,
	const QString&                                  label,
	double                                          min,
	double                                          max,
	double                                          step,
	const std::function<float&(assetlib::BGrass&)>& value,
	bool                                            resizesPatch)
{
	auto* box = new QDoubleSpinBox(form->parentWidget());
	box->setObjectName(QStringLiteral("Grass_%1").arg(QString::fromLatin1(key)));
	box->setRange(min, max);
	box->setSingleStep(step);
	box->setDecimals(3);
	box->setKeyboardTracking(false);
	form->addRow(label, box);

	connect(box, &QDoubleSpinBox::valueChanged, this, [this, value, resizesPatch](double v) {
		if (m_Syncing)
			return;
		value(m_Look) = static_cast<float>(v);
		Edited(resizesPatch);
	});

	m_Syncs.emplace_back([this, box, value] { box->setValue(value(m_Look)); });
}

void
GrassEditorWindow::AddCount(
	QFormLayout*                                       form,
	const char*                                        key,
	const QString&                                     label,
	int                                                min,
	int                                                max,
	const std::function<uint32_t&(assetlib::BGrass&)>& value)
{
	auto* box = new QSpinBox(form->parentWidget());
	box->setObjectName(QStringLiteral("Grass_%1").arg(QString::fromLatin1(key)));
	box->setRange(min, max);
	box->setKeyboardTracking(false);
	form->addRow(label, box);

	connect(box, &QSpinBox::valueChanged, this, [this, value](int v) {
		if (m_Syncing)
			return;
		value(m_Look) = static_cast<uint32_t>(v);
		Edited(false);
	});

	m_Syncs.emplace_back([this, box, value] { box->setValue(static_cast<int>(value(m_Look))); });
}

void
GrassEditorWindow::AddColour(
	QFormLayout*                                        form,
	const char*                                         key,
	const QString&                                      label,
	const std::function<glm::vec3&(assetlib::BGrass&)>& value)
{
	auto* button = new QToolButton(form->parentWidget());
	button->setObjectName(QStringLiteral("Grass_%1").arg(QString::fromLatin1(key)));
	form->addRow(label, button);

	connect(button, &QToolButton::clicked, this, [this, button, value, label] {
		const QColor picked = QColorDialog::getColor(SwatchOf(value(m_Look)), this, label);
		if (!picked.isValid())
			return;
		value(m_Look) = LinearOf(picked);
		Paint(button, value(m_Look));
		Edited(false);
	});

	m_Syncs.emplace_back([this, button, value] { Paint(button, value(m_Look)); });
}

void
GrassEditorWindow::SyncFields()
{
	m_Syncing = true;
	m_Material->setText(QString::fromStdString(m_Look.material));
	for (const std::function<void()>& sync : m_Syncs) sync();
	m_Syncing = false;
}

void
GrassEditorWindow::OpenAsset(std::string_view key)
{
	if (key == m_Key)
		return;

	auto look = assetlib::BGrass();
	try
	{
		look = m_Host.GetStore().Load<assetlib::BGrass>(std::string(key));
	}
	catch (const std::exception& error)
	{
		SetStatus(
			editor::Localize(
				m_Host.GetLanguageResolver(),
				"bernini.grass.read_failed",
				{ std::string(key), error.what() },
				"'{0}' could not be read: {1}"));
		return;
	}

	if (!CanClose())
		return;

	ReleasePreview();
	m_Key   = std::string(key);
	m_Look  = look;
	m_Saved = std::move(look);
	SetStatus({});
	SyncFields();
	UpdateTitle();
	m_Stage->setCurrentIndex(1);

	m_Orbit.FocusOn(glm::vec3(0.0f, 0.3f, 0.0f), c_StartRadius, 0.0f, c_StartPitch);
	UpdateCamera();
	BuildPreview();
}

bool
GrassEditorWindow::Save()
{
	if (m_Key.empty() || !IsDirty())
		return true;

	try
	{
		m_Host.GetStore().Save(m_Look, m_Key);
	}
	catch (const std::exception& error)
	{
		SetStatus(
			editor::Localize(
				m_Host.GetLanguageResolver(),
				"bernini.grass.save_failed",
				{ m_Key, error.what() },
				"'{0}' could not be saved: {1}"));
		return false;
	}

	m_Saved = m_Look;
	UpdateTitle();
	m_Host.AssetChanged(m_Key);
	return true;
}

void
GrassEditorWindow::Revert()
{
	if (m_Key.empty())
		return;

	try
	{
		m_Saved = m_Host.GetStore().Load<assetlib::BGrass>(m_Key);
	}
	catch (const std::exception& error)
	{
		SetStatus(
			editor::Localize(
				m_Host.GetLanguageResolver(),
				"bernini.grass.read_failed",
				{ m_Key, error.what() },
				"'{0}' could not be read: {1}"));
		return;
	}

	const bool resized =
		editor::PreviewPatchFor(m_Look).size != editor::PreviewPatchFor(m_Saved).size;
	m_Look = m_Saved;
	SyncFields();
	Edited(resized);
}

void
GrassEditorWindow::Edited(bool resizesPatch)
{
	UpdateTitle();
	if (resizesPatch)
		BuildPreview();
	else
		PushLook();
}

void
GrassEditorWindow::PushLook()
{
	if (m_Key.empty())
		return;

	// Nothing drawn to change: a look that could not be drawn is grown again from the edit.
	if (!m_Drawn.has_value() || !m_Patch.IsValid())
	{
		BuildPreview();
		return;
	}

	QString refusal;
	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef&) {
		try
		{
			if (context.assets.SetGrassLook(m_Key, m_Look))
				m_Drawn = m_Look;
			else
				m_Drawn.reset();
		}
		catch (const std::exception& error)
		{
			refusal = QString::fromUtf8(error.what());
		}
	});

	if (!refusal.isEmpty())
	{
		SetStatus(
			editor::Localize(
				m_Host.GetLanguageResolver(),
				"bernini.grass.refused",
				{ refusal },
				"The preview keeps the last look it could draw: {0}"));
		return;
	}

	SetStatus({});
	if (!m_Drawn.has_value())
		BuildPreview();
}

void
GrassEditorWindow::BuildPreview()
{
	ReleasePreview();
	if (m_Key.empty())
		return;

	if (m_Look.material.empty())
	{
		SetStatus(
			editor::Localize(
				m_Host.GetLanguageResolver(),
				"bernini.grass.no_material",
				"The look names no material, so there is nothing to draw yet."));
		return;
	}

	QString refusal;
	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		try
		{
			m_Patch = context.assets.CreateGrassPatch(
				editor::PreviewPatchFor(m_Look),
				m_Key,
				m_Look,
				m_Ground);

			// The patch lies in XY facing +Z, as a plane does, so it is laid flat on its back.
			m_Instance = context.assets.CreateInstance(
				view,
				m_Patch,
				glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));

			// When something else already held this look the patch shares it as drawn, so the edit
			// is put on it; and a look that could not be created answers false.
			if (context.assets.SetGrassLook(m_Key, m_Look))
				m_Drawn = m_Look;
		}
		catch (const std::exception& error)
		{
			refusal = QString::fromUtf8(error.what());
		}
	});

	if (!refusal.isEmpty())
	{
		SetStatus(
			editor::Localize(
				m_Host.GetLanguageResolver(),
				"bernini.grass.cannot_draw",
				{ refusal },
				"The look cannot be drawn: {0}"));
		return;
	}

	SetStatus(
		m_Drawn.has_value() ? QString() :
							  editor::Localize(
								  m_Host.GetLanguageResolver(),
								  "bernini.grass.not_drawn",
								  "The look cannot be drawn; editor.log says why."));
}

void
GrassEditorWindow::ReleasePreview()
{
	m_Drawn.reset();
	if (!m_Patch.IsValid())
		return;

	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		try
		{
			if (m_Instance.IsValid())
				context.assets.DestroyInstance(view, m_Instance);
			context.assets.ReleaseGeom(m_Patch);
		}
		catch (const std::exception& error)
		{
			qWarning("GrassEditor: failed to release the patch: %s", error.what());
		}
	});

	m_Instance = {};
	m_Patch    = {};
}

void
GrassEditorWindow::SetStatus(const QString& text)
{
	m_Status->setText(text);
	m_Status->setVisible(!text.isEmpty());
}

void
GrassEditorWindow::UpdateTitle()
{
	const QString key = QString::fromStdString(m_Key);
	m_Title->setText(IsDirty() ? key + QStringLiteral(" *") : key);
	m_Save->setEnabled(IsDirty());
	m_Revert->setEnabled(IsDirty());
}

void
GrassEditorWindow::UpdateCamera()
{
	const int   w      = m_Viewport->width();
	const int   h      = m_Viewport->height();
	const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;

	// The orbit derives its far plane from the focus sphere, which is the near field; the patch
	// runs out to its fade end, and the whole of the thinning is what there is to judge.
	bgl::Camera camera = m_Orbit.GetCamera(aspect);
	camera.Perspective(
		glm::radians(60.0f),
		aspect,
		0.05f,
		2.0f * editor::PreviewPatchFor(m_Look).size);
	m_Viewport->SetCamera(camera);
}

void
GrassEditorWindow::PushWind()
{
	const bgl::WindDesc wind = editor::PreviewWind(
		static_cast<float>(m_WindStrength->value()),
		static_cast<float>(m_WindHeading->value()),
		static_cast<float>(m_WindGustStrength->value()),
		static_cast<float>(m_WindGustScale->value()),
		static_cast<float>(m_WindGustSpeed->value()));

	m_Viewport->Invoke([&](editor::RenderContext&, const bgl::SceneViewRef& view) {
		try
		{
			view->SetWind(wind);
		}
		catch (const std::exception& error)
		{
			qWarning("GrassEditor: the wind was refused: %s", error.what());
		}
	});
}

void
GrassEditorWindow::Tick()
{
	m_Viewport->SetTime(static_cast<float>(m_Elapsed.elapsed()) / 1000.0f);
}

std::vector<std::string>
GrassEditorWindow::GetHeldAssets() const
{
	auto held = std::vector<std::string>();
	if (m_Key.empty())
		return held;

	held.push_back(m_Key);
	if (!m_Look.material.empty())
		held.push_back(m_Look.material);

	for (const QString& path : editor::GetHeldOpenEnvironment(m_Environment))
	{
		try
		{
			held.push_back(m_Host.GetStore().KeyFor(std::filesystem::path(path.toStdWString())));
		}
		catch (const std::exception&)
		{
			// An environment outside the project is not the project's to hold.
		}
	}
	return held;
}

bool
GrassEditorWindow::CanClose()
{
	return Save();
}

void
GrassEditorWindow::SetActive(bool active)
{
	m_Viewport->SetRenderingEnabled(active);
	if (active)
		m_Clock->start();
	else
		m_Clock->stop();
}

void
GrassEditorWindow::OnAssetChanged(std::string_view key)
{
	// Written by someone else while nothing here is pending: show what the file now says. Our own
	// save comes back through here too, and then the file is what is already on screen.
	if (key != m_Key || IsDirty())
		return;

	try
	{
		const auto look = m_Host.GetStore().Load<assetlib::BGrass>(m_Key);
		if (look == m_Saved)
			return;

		const bool resized =
			editor::PreviewPatchFor(m_Look).size != editor::PreviewPatchFor(look).size;
		m_Saved = look;
		m_Look  = look;
		SyncFields();
		Edited(resized);
	}
	catch (const std::exception& error)
	{
		qWarning(
			"GrassEditor: '%s' changed and could not be read again: %s",
			m_Key.c_str(),
			error.what());
	}
}

bool
GrassEditorWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (watched != m_Viewport)
		return editor::AssetEditorPanel::eventFilter(watched, event);

	switch (event->type())
	{
	case QEvent::MouseButtonPress:
	{
		const auto* mouse = static_cast<QMouseEvent*>(event);
		m_DragButton      = mouse->button();
		m_LastMousePos    = mouse->position().toPoint();
		return true;
	}
	case QEvent::MouseButtonRelease:
		m_DragButton = Qt::NoButton;
		return true;
	case QEvent::MouseMove:
	{
		const auto*  mouse = static_cast<QMouseEvent*>(event);
		const QPoint pos   = mouse->position().toPoint();
		const QPoint delta = pos - m_LastMousePos;
		m_LastMousePos     = pos;

		if (m_DragButton == Qt::LeftButton)
			m_Orbit.Orbit(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
		else if (m_DragButton == Qt::MiddleButton || m_DragButton == Qt::RightButton)
			m_Orbit.Pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
		else
			return true;

		UpdateCamera();
		return true;
	}
	case QEvent::Wheel:
		m_Orbit.Dolly(
			static_cast<float>(static_cast<QWheelEvent*>(event)->angleDelta().y()) / 120.0f);
		UpdateCamera();
		return true;
	case QEvent::Resize:
		UpdateCamera();
		break;
	default:
		break;
	}
	return editor::AssetEditorPanel::eventFilter(watched, event);
}
