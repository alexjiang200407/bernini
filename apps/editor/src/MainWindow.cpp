#include "MainWindow.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDockWidget>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <qcontainerfwd.h>

#include "Plugins/EditorHost.h"
#include "Plugins/EditorRegistry.h"
#include "Plugins/plugin_loader.h"
#include "Render/Renderer.h"
#include "Thumbnails/AssetThumbnailCache.h"
#include "Windows/ContentExplorer/ContentExplorerWindow.h"
#include "Windows/GpuTiming/GpuTimingWindow.h"
#include "Windows/Plugins/PluginsWindow.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "main_window_ui.h"
#include "util/follows_project.h"
#include "util/frame_stats_text.h"
#include "util/held_open_assets.h"
#include "util/panel_visibility.h"
#include "util/project_dialogs.h"
#include "util/recent_projects.h"
#include "util/surface_relaunch.h"
#include "util/window_title.h"
#include <algorithm>
#include <array>
#include <assetlib/Project.h>
#include <assetlib/cancel.h>
#include <assetlib/progress.h>
#include <default_editor/plugin.h>
#include <editor_plugin_api/IEditorRegistry.h>
#include <editor_plugin_api/TranslationCatalog.h>
#include <editor_sdk/BackgroundTask.h>
#include <editor_sdk/environment.h>

#include <QActionGroup>
#include <QMenuBar>
#include <assetlib/AssetStore.h>
#include <assetlib/asset_import.h>
#include <assetlib/migrate.h>
#include <assetlib/reimport.h>
#include <assetlib/texture_prune.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <core/err/util.h>
#include <core/glm.h>
#include <core/settings/Settings.h>

#include "util/editor_config.h"
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <editor_plugin_api/EditorPanel.h>
#include <editor_plugin_api/LanguageResolver.h>
#include <exception>
#include <filesystem>
#include <functional>
#include <gamelib/AssetManager.h>

#include "Startup/startup_labels.h"

#include <QDebug>
#include <QKeySequence>
#include <bgl/PassTiming.h>
#include <core/str/str.h>
#include <memory>
#include <optional>
#include <qaction.h>
#include <qlist.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qnumeric.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qtypes.h>
#include <qwidget.h>
#include <stdexcept>
#include <string>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

MainWindow::MainWindow(
	std::unique_ptr<editor::plugins::PluginSession> plugins,
	assetlib::Project                               project,
	std::filesystem::path                           configPath,
	background::ProgressSink                        startup,
	QWidget*                                        parent) :
	QMainWindow(parent), m_StartupProgress(std::move(startup)), m_Plugins(std::move(plugins))
{
	try
	{
		Build(configPath.empty() ? editor::DefaultConfigPath() : configPath, std::move(project));
	}
	catch (...)
	{
		// A viewport is already a child of this window, and ~MainWindow does not run for a
		// constructor that threw -- so without this the unwind reaches ~RenderTargetWindow with
		// m_Renderer gone. A function-try-block cannot do it: its handler runs after the members
		// have been destroyed.
		ReleaseRenderResources();
		throw;
	}
}

void
MainWindow::Build(const std::filesystem::path& configPath, assetlib::Project project)
{
	ZoneScopedN("editor build window");

	m_Ui = editor::BuildMainWindowUi(this);

	connect(m_Ui.newProject, &QAction::triggered, this, &MainWindow::NewProject);
	connect(m_Ui.openProject, &QAction::triggered, this, &MainWindow::OpenProject);
	connect(m_Ui.cleanUnusedTextures, &QAction::triggered, this, &MainWindow::CleanUnusedTextures);
	connect(m_Ui.exit, &QAction::triggered, this, &QWidget::close);

	m_RecentProjectsFile = editor::RecentProjectsFileBeside(configPath);

	{
		core::Settings settings(configPath);

		m_InstanceName =
			QString::fromStdString(settings["instanceName"].GetOrDefault(std::string()));

		// Builds every viewport offscreen. For editor_tests, which cannot realise a native window;
		// a headless editor still creates the device and renders, it just presents nothing.
		const bool headless = settings["headless"].GetOrDefault(false);
		m_Headless          = headless;

		const auto gfxSettings = settings["graphics"];

		auto gfxOpts             = bgl::GraphicsOptions();
		gfxOpts.enableDebugLayer = gfxSettings["enableDebugLayer"].GetOrDefault(false);
		gfxOpts.enableGPUValidationLayer =
			gfxSettings["enableGPUBasedValidation"].GetOrDefault(false);
		gfxOpts.enablePixDebug = gfxSettings["enablePixDebug"].GetOrDefault(false);
		gfxOpts.strictError    = gfxSettings["strictError"].GetOrDefault(false);
		gfxOpts.logLevel       = static_cast<bgl::GraphicsOptions::LogLevel>(
			gfxSettings["logLevel"].GetOrDefault(static_cast<int>(gfxOpts.logLevel)));
		gfxOpts.maxCbvSrvUavs = gfxSettings["maxCbvSrvUavs"].GetOrDefault(gfxOpts.maxCbvSrvUavs);
		gfxOpts.maxBuffers    = gfxSettings["maxBuffers"].GetOrDefault(gfxOpts.maxBuffers);
		gfxOpts.maxSrvs       = gfxSettings["maxSrvs"].GetOrDefault(gfxOpts.maxSrvs);
		gfxOpts.maxRtvs       = gfxSettings["maxRtvs"].GetOrDefault(gfxOpts.maxRtvs);
		gfxOpts.maxDsvs       = gfxSettings["maxDsvs"].GetOrDefault(gfxOpts.maxDsvs);
		gfxOpts.maxTextures   = gfxSettings["maxTextures"].GetOrDefault(gfxOpts.maxTextures);

		if (gfxSettings["enableShaderCache"].GetOrDefault(true))
			gfxOpts.shaderCacheDir = "shadercache";

		// This project's alone. Surfaces are registered inside CreateGraphics and their programs are
		// generated from what was there then, so a project with other shaders is opened by
		// restarting into it -- see AskHowToOpen.
		gfxOpts.surfaceShaderDir = editor::ShadersDirectoryOf(project.GetProjectFile());
		m_SurfaceShaderDir       = gfxOpts.surfaceShaderDir;

		// The editor's one Scene. Every viewport (the Material Editor's model preview, the Animation
		// Editor's) renders it through a SceneView of its own, so geometry, textures and materials
		// are pooled here once and these budgets must cover all of them together.
		auto sceneDesc             = bgl::SceneDesc();
		auto sceneSettings         = settings["scene"];
		sceneDesc.initialGeom      = sceneSettings["initialGeom"].GetOrDefault(256);
		sceneDesc.initialMeshlets  = sceneSettings["initialMeshlets"].GetOrDefault(32768);
		sceneDesc.initialSubmeshes = sceneSettings["initialSubmeshes"].GetOrDefault(512);
		sceneDesc.initialVertexBufferByteSize =
			sceneSettings["initialVertexBufferByteSize"].GetOrDefault(33554432);
		sceneDesc.initialIndices      = sceneSettings["initialIndices"].GetOrDefault(2000000);
		sceneDesc.initialPbrMaterials = sceneSettings["initialPbrMaterials"].GetOrDefault(256);
		sceneDesc.initialLoosePbrMaterials =
			sceneSettings["initialLoosePbrMaterials"].GetOrDefault(256);
		sceneDesc.initialSurfaceMaterials =
			sceneSettings["initialSurfaceMaterials"].GetOrDefault(64);

		// One step, not one per pipeline: bgl builds them all inside CreateGraphics and a warm
		// shader cache turns the whole stretch into milliseconds. Cold it is seconds, which is why
		// the bar below has to keep moving even though nothing here can say how far along it is.
		if (m_StartupProgress)
			m_StartupProgress(0, 0, "Compiling shaders...");

		// The renderer owns the Graphics and the Scene and, once threaded, is the only thing that
		// touches them. Every viewport and the thumbnail cache render through it.
		//
		// Pumping while it builds, because compiling the pipelines is most of a cold start and the
		// screen saying so is on this thread. Nothing of this window is shown yet, so the events
		// that run are the screen's own.
		m_Renderer = std::make_unique<Renderer>(
			gfxOpts,
			sceneDesc,
			m_StartupProgress ? RendererWait::kPumpEventLoop : RendererWait::kBlock);

		m_SurfaceCount =
			m_Renderer->Invoke([&] { return m_Renderer->GetGraphics()->GetSurfaceTypes().size(); });

		// The preview look, each knob overridable per viewport; absent keeps what `sky` came with.
		const auto readSky = [](const auto& section, editor::SkyPresentation sky) {
			if (auto mip = section["skyMipLevel"])
				sky.mipLevel = mip.GetOrDefault(sky.mipLevel.value_or(0u));
			sky.opacity      = section["backdropOpacity"].GetOrDefault(sky.opacity);
			sky.backdropGrey = section["backdropGrey"].GetOrDefault(sky.backdropGrey);
			sky.followsView  = section["followView"].GetOrDefault(sky.followsView);
			return sky;
		};

		// Each absent key keeps the default, so a partial section overrides only what it names.
		// Range checks are the viewport's, at creation.
		const auto readBloom = [](const auto& section) {
			auto       bloom = BloomConfig();
			const auto node  = section["bloom"];
			bloom.enabled    = node["enabled"].GetOrDefault(bloom.enabled);
			auto& s          = bloom.settings;
			s.intensity      = node["intensity"].GetOrDefault(s.intensity);
			s.threshold      = node["threshold"].GetOrDefault(s.threshold);
			s.softKnee       = node["softKnee"].GetOrDefault(s.softKnee);
			s.scatter        = node["scatter"].GetOrDefault(s.scatter);
			return bloom;
		};

		// The CDL's per-channel values are objects -- { "r": .., "g": .., "b": .. } -- so a partial
		// one overrides only the channels it names, like the rest of the section.
		const auto readRgb = [](const auto& node, glm::vec3 rgb) {
			rgb.r = node["r"].GetOrDefault(rgb.r);
			rgb.g = node["g"].GetOrDefault(rgb.g);
			rgb.b = node["b"].GetOrDefault(rgb.b);
			return rgb;
		};

		const auto readColorGrade = [&readRgb](const auto& section) {
			auto       grade     = ColorGradeConfig();
			const auto node      = section["colorGrade"];
			grade.enabled        = node["enabled"].GetOrDefault(grade.enabled);
			auto& s              = grade.settings;
			s.temperature        = node["temperature"].GetOrDefault(s.temperature);
			s.tint               = node["tint"].GetOrDefault(s.tint);
			s.slope              = readRgb(node["slope"], s.slope);
			s.offset             = readRgb(node["offset"], s.offset);
			s.power              = readRgb(node["power"], s.power);
			s.saturation         = node["saturation"].GetOrDefault(s.saturation);
			s.contrast           = node["contrast"].GetOrDefault(s.contrast);
			s.vignetteIntensity  = node["vignetteIntensity"].GetOrDefault(s.vignetteIntensity);
			s.vignetteSmoothness = node["vignetteSmoothness"].GetOrDefault(s.vignetteSmoothness);
			return grade;
		};

		// temporalAA, renderScale and taaReconstructionWidth are each viewport's own rather than
		// graphics-wide -- see docs/taa.md. `headless` is every viewport together: a headless editor
		// is a whole editor built without windows, which is the only shape a test can construct.
		const auto readViewport = [&](const auto& section) {
			auto       viewport             = editor::ViewportDesc();
			const auto bloom                = readBloom(section);
			const auto grade                = readColorGrade(section);
			viewport.initialInstances       = section["initialPreviewInstances"].GetOrDefault(16u);
			viewport.taaEnabled             = section["temporalAA"].GetOrDefault(true);
			viewport.renderScale            = section["renderScale"].GetOrDefault(1.0f);
			viewport.taaReconstructionWidth = section["taaReconstructionWidth"].GetOrDefault(0.4f);
			viewport.bloomEnabled           = bloom.enabled;
			viewport.bloom                  = bloom.settings;
			viewport.colorGradeEnabled      = grade.enabled;
			viewport.colorGrade             = grade.settings;
			return viewport;
		};

		auto matSettings               = settings["materialEditor"];
		auto defaultConfig             = editor::defaults::Config();
		defaultConfig.materialViewport = readViewport(matSettings);
		defaultConfig.materialEnvironment.environmentMap =
			matSettings["environmentMap"].GetOrDefault(std::string());
		defaultConfig.materialEnvironment.dataRoot =
			matSettings["dataRoot"].GetOrDefault(std::string());
		defaultConfig.materialEnvironment.sky = readSky(matSettings, editor::SkyPresentation());

		// Absent, and the .benv's own derived exposure stands -- which is the correct one for its maps.
		if (auto exposure = matSettings["exposure"])
			defaultConfig.materialEnvironment.exposureOverride = exposure.GetOrDefault(1.0f);

		auto thumbSettings           = settings["thumbnails"];
		auto thumbDesc               = AssetThumbnailDesc();
		thumbDesc.renderer           = m_Renderer.get();
		thumbDesc.dimension          = thumbSettings["dimension"].GetOrDefault(256u);
		thumbDesc.initialInstances   = thumbSettings["initialInstances"].GetOrDefault(256u);
		thumbDesc.env.environmentMap = thumbSettings["environmentMap"].GetOrDefault(std::string());
		thumbDesc.env.dataRoot       = thumbSettings["dataRoot"].GetOrDefault(std::string());
		thumbDesc.env.sky            = readSky(thumbSettings, editor::SkyPresentation());
		thumbDesc.pluginProvider     = [this](const std::string_view extension) {
			return m_Plugins->Contributions().FindThumbnailProvider(extension);
		};

		if (auto exposure = thumbSettings["exposure"])
			thumbDesc.env.exposureOverride = exposure.GetOrDefault(1.0f);

		auto animSettings         = settings["animationEditor"];
		defaultConfig.rigViewport = readViewport(animSettings);
		// Falls back to the material editor's environment: both are asset previews wanting the
		// same neutral look, and a config predating this panel would otherwise light it with
		// nothing -- which draws black and says nothing.
		defaultConfig.rigEnvironment.environmentMap = animSettings["environmentMap"].GetOrDefault(
			matSettings["environmentMap"].GetOrDefault(std::string()));
		defaultConfig.rigEnvironment.dataRoot = animSettings["dataRoot"].GetOrDefault(
			matSettings["dataRoot"].GetOrDefault(std::string()));
		defaultConfig.rigEnvironment.sky =
			readSky(animSettings, defaultConfig.materialEnvironment.sky);

		// Absent, and the .benv's own derived exposure stands -- which is the correct one for its maps.
		if (auto exposure = animSettings["exposure"])
			defaultConfig.rigEnvironment.exposureOverride = exposure.GetOrDefault(1.0f);

		m_Plugins->RegisterEditorPlugins(editor::defaults::CreatePlugin(defaultConfig));

		// Parented so the held-open walk reaches it: it is lit by a `.benv` like the viewports are.
		m_Thumbnails = std::make_unique<AssetThumbnailCache>(std::move(thumbDesc), this);
	}

	setDockNestingEnabled(true);
	setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

	m_ContentExplorerDock = new QDockWidget("Content Explorer", this);
	m_ContentExplorerDock->setObjectName("ContentExplorerDock");

	// Not floatable: its title bar sits directly under the separator the user drags to make it
	// taller, and a grab that lands low would otherwise tear it out into a window of its own -- which
	// on macOS is a separate top-level that goes behind the editor.
	m_ContentExplorerDock->setFeatures(
		QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);

	// Asked at each deletion, so there is no copy of the answer to go stale, and walked rather than
	// listed so a panel added later is covered without anyone remembering it.
	m_ContentExplorer = new ContentExplorerWindow(m_ContentExplorerDock, [this] {
		return editor::GetAssetsHeldOpen(this);
	});
	m_ContentExplorer->SetThumbnails(m_Thumbnails.get());
	connect(
		m_ContentExplorer,
		&ContentExplorerWindow::MaterialBaked,
		this,
		[this](const QString& key) {
			if (m_EditorHost)
				m_EditorHost->AssetChanged(key.toStdString());
		});
	m_ContentExplorer->SetPluginImporter(
		[this](const std::filesystem::path& source) {
			std::string extension = source.extension().string();
			std::ranges::transform(extension, extension.begin(), [](const unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return m_Plugins->Contributions().FindImporter(extension) != nullptr;
		},
		[this](const std::filesystem::path& source, const std::string_view target) {
			if (m_EditorHost == nullptr)
				return;
			std::string extension = source.extension().string();
			std::ranges::transform(extension, extension.begin(), [](const unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			const editor::ImporterDesc* importer =
				m_Plugins->Contributions().FindImporter(extension);
			if (importer == nullptr)
				return;
			try
			{
				importer->importer->Import(*m_EditorHost, source, target);
			}
			catch (const std::exception& error)
			{
				QMessageBox::warning(this, "Plugin Import", error.what());
			}
		});
	m_ContentExplorer->SetPluginActions(
		[this](QMenu& menu, const std::vector<std::string>& selection) {
			if (m_EditorHost == nullptr)
				return;
			for (const editor::ActionDesc& desc : m_Plugins->Contributions().Actions())
			{
				if (desc.extensions.empty())
					continue;
				const bool matches = std::ranges::all_of(selection, [&](const std::string& key) {
					std::string extension = std::filesystem::path(key).extension().string();
					std::ranges::transform(extension, extension.begin(), [](const unsigned char c) {
						return static_cast<char>(std::tolower(c));
					});
					return std::ranges::find(desc.extensions, extension) != desc.extensions.end();
				});
				if (!matches)
					continue;
				bool enabled = false;
				try
				{
					enabled = desc.action->IsEnabled(*m_EditorHost, selection);
				}
				catch (const std::exception& error)
				{
					qWarning("Plugin action predicate failed: %s", error.what());
				}
				QAction* action =
					menu.addAction(desc.title.Resolve(m_EditorHost->GetLanguageResolver()));
				action->setEnabled(enabled);
				const editor::ActionDesc* descriptor = &desc;
				connect(action, &QAction::triggered, &menu, [this, descriptor, selection] {
					try
					{
						descriptor->action->Invoke(*m_EditorHost, selection);
					}
					catch (const std::exception& error)
					{
						QMessageBox::warning(this, "Plugin Action", error.what());
					}
				});
			}
		});

	connect(
		m_ContentExplorer,
		&ContentExplorerWindow::AssetOpenRequested,
		this,
		[this](const QString& key) { OpenPluginAsset(key.toStdString()); });

	m_ContentExplorer->setMinimumSize(0, 0);
	m_ContentExplorerDock->setWidget(m_ContentExplorer);
	addDockWidget(Qt::BottomDockWidgetArea, m_ContentExplorerDock);

	m_Ui.windowMenu->addAction(m_ContentExplorerDock->toggleViewAction());
	m_Ui.windowMenu->addSeparator();
	SetUpGpuTimingEntry();
	SetUpPluginsEntry();
	SetUpPluginContributions();

	SetActiveProject(std::move(project));

	SetUpRenderMenu();

	// Startup is over: a project opened from the menu from here on gets the modal screen, not the
	// one main() is about to close.
	m_StartupProgress = {};
}

background::TaskResult
MainWindow::RunBehindScreen(
	const QString&                                    title,
	const std::function<void(background::Progress&)>& work,
	background::Cancellable                           cancellable)
{
	if (m_StartupProgress)
		return background::RunReporting(m_StartupProgress, work);

	return background::RunWithLoadingScreen(this, title, work, cancellable);
}

void
MainWindow::SetUpRenderMenu()
{
	QMenu* render = menuBar()->addMenu("Render");

	// Each viewport is configured on its own, so the menu offers the toggle if any of them has
	// something to toggle. A window configured without it ignores the call rather than throwing.
	bool anyTaa = false;
	for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
		anyTaa = anyTaa || view->IsTaaAvailable();

	auto* taa = render->addAction("Temporal Antialiasing");
	taa->setCheckable(true);
	taa->setChecked(anyTaa);

	// Disabled rather than hidden, so the answer to "why can I not turn this on" is where the
	// question gets asked.
	taa->setEnabled(anyTaa);
	taa->setStatusTip(
		anyTaa ? "Jitter the projection and accumulate a temporal history in the viewports." :
				 "No viewport enabled temporalAA in config.json, so none allocated a history.");

	connect(render, &QMenu::aboutToShow, this, [this, taa] {
		bool available = false;
		for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
			available = available || view->IsTaaAvailable();
		const QSignalBlocker blocker(taa);
		taa->setEnabled(available);
		taa->setChecked(m_TaaOverride.value_or(available));
		taa->setStatusTip(
			available ?
				"Jitter the projection and accumulate a temporal history in the viewports." :
				"No open viewport allocated temporal-AA history.");
	});

	connect(taa, &QAction::toggled, this, [this](bool enabled) {
		m_TaaOverride = enabled;
		for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
			view->SetTaaEnabled(enabled);
	});

	auto* outline = render->addAction("Selection Outline");
	outline->setCheckable(true);
	outline->setChecked(true);
	outline->setStatusTip("Contour the selected submesh in the viewports.");

	connect(outline, &QAction::toggled, this, [this](bool enabled) {
		m_OutlineEnabled = enabled;
		for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
			view->SetOutlineEnabled(enabled);
	});

	// On and off only: how a viewport blooms is config.json's, so a comparison against itself is
	// the one thing asked of the menu. Checked when config.json started any viewport with it.
	bool anyBloom = false;
	for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
		anyBloom = anyBloom || view->IsBloomEnabled();

	auto* bloom = render->addAction("Bloom");
	bloom->setCheckable(true);
	bloom->setChecked(anyBloom);
	bloom->setStatusTip(
		"Spill the viewports' bright pixels into a glow, ahead of the display curve. How they "
		"bloom is each viewport's `bloom` section in config.json.");

	connect(bloom, &QAction::toggled, this, [this](bool enabled) {
		m_BloomOverride = enabled;
		for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
			view->SetBloomEnabled(enabled);
	});

	// As bloom: the grade itself is each viewport's config.json section.
	bool anyGrade = false;
	for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
		anyGrade = anyGrade || view->IsColorGradeEnabled();

	auto* grade = render->addAction("Color Grade");
	grade->setCheckable(true);
	grade->setChecked(anyGrade);
	grade->setStatusTip(
		"White-balance and grade the viewports ahead of the display curve. The grade is each "
		"viewport's `colorGrade` section in config.json.");

	connect(grade, &QAction::toggled, this, [this](bool enabled) {
		m_ColorGradeOverride = enabled;
		for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
			view->SetColorGradeEnabled(enabled);
	});

	auto* timing = render->addAction("GPU Pass Timing");
	timing->setCheckable(true);
	timing->setChecked(false);
	timing->setStatusTip(
		"Time every pass of the viewports' frames on the GPU, for Log GPU Pass Timings to write "
		"out. Costs a little per frame, which is why it is off until asked for.");

	connect(timing, &QAction::toggled, this, [this](bool enabled) {
		for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
			view->SetGpuTimingEnabled(enabled);
	});

	m_GpuTimingAction = timing;

	// One frame's table into editor.log. Needs timing on, so it follows the toggle.
	auto* logTiming = render->addAction("Log GPU Pass Timings");
	logTiming->setShortcut(QKeySequence("Ctrl+Shift+T"));
	logTiming->setEnabled(false);
	logTiming->setStatusTip(
		"Write the rendering viewport's next per-pass GPU breakdown to editor.log.");
	connect(timing, &QAction::toggled, logTiming, &QAction::setEnabled);
	connect(logTiming, &QAction::triggered, this, [this] { m_LogNextPassTimings = true; });

	SetUpRenderScaleMenu(render);
}

void
MainWindow::SetUpRenderScaleMenu(QMenu* render)
{
	static constexpr std::array c_Scales = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };

	QMenu* scale = render->addMenu("Render Scale");
	scale->setStatusTip(
		"Render the viewports at a fraction of their window and let the temporal resolve "
		"reconstruct it, to judge a resolution-dependent artifact on a display that does not have "
		"that density.");

	auto* group = new QActionGroup(scale);
	group->setExclusive(true);

	// The viewports move together, so the checked entry follows whichever was configured first. A
	// config.json scale outside the list leaves none of them checked, which is honest.
	const QList<RenderTargetWindow*> views = findChildren<RenderTargetWindow*>();
	const float current = views.isEmpty() ? 1.0f : views.first()->GetRenderScale();

	for (const float factor : c_Scales)
	{
		QAction* action = scale->addAction(QString("%1x").arg(factor));
		action->setCheckable(true);
		action->setChecked(qFuzzyCompare(factor, current));
		group->addAction(action);

		connect(scale, &QMenu::aboutToShow, action, [this, action, factor] {
			const auto  views   = findChildren<RenderTargetWindow*>();
			const float current = m_RenderScaleOverride.value_or(
				views.isEmpty() ? 1.0f : views.first()->GetRenderScale());
			action->setChecked(qFuzzyCompare(factor, current));
		});

		connect(action, &QAction::triggered, this, [this, factor]() {
			m_RenderScaleOverride = factor;
			for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
				view->SetRenderScale(factor);
		});
	}

	SetUpReconstructionWidthMenu(render);
}

// Beside the render scale because it is only legible against one: below 1.0 the resolve builds each
// output pixel out of the jittered render samples nearest it, and this is how wide "nearest" is. At
// scale 1.0 every output pixel has a sample of its own and nothing here moves the image.
void
MainWindow::SetUpReconstructionWidthMenu(QMenu* render)
{
	// Either side of the 0.4 a target ships with, and up to twice it: on hashed alpha the wide end is
	// where a trail stops being visible, and stopping the menu at the default's own neighbourhood
	// would put that out of reach.
	static constexpr std::array c_Widths = { 0.25f, 0.4f, 0.6f, 0.8f, 1.0f };

	QMenu* width = render->addMenu("TAA Reconstruction Width");
	width->setStatusTip(
		"How wide a kernel the temporal resolve rebuilds each output pixel with, in output pixels. "
		"Narrower is sharper on a held frame and slower to settle on a moving one; it has no "
		"effect "
		"at a render scale of 1.");

	auto* group = new QActionGroup(width);
	group->setExclusive(true);

	const QList<RenderTargetWindow*> views = findChildren<RenderTargetWindow*>();
	const float current = views.isEmpty() ? 0.4f : views.first()->GetTaaReconstructionWidth();

	for (const float value : c_Widths)
	{
		QAction* action = width->addAction(QString("%1 px").arg(value));
		action->setCheckable(true);
		action->setChecked(qFuzzyCompare(value, current));
		group->addAction(action);

		connect(width, &QMenu::aboutToShow, action, [this, action, value] {
			const auto  views   = findChildren<RenderTargetWindow*>();
			const float current = m_ReconstructionWidthOverride.value_or(
				views.isEmpty() ? 0.4f : views.first()->GetTaaReconstructionWidth());
			action->setChecked(qFuzzyCompare(value, current));
		});

		connect(action, &QAction::triggered, this, [this, value]() {
			m_ReconstructionWidthOverride = value;
			for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
				view->SetTaaReconstructionWidth(value);
		});
	}
}

void
MainWindow::closeEvent(QCloseEvent* event)
{
	if (!CanClosePluginPanels())
	{
		event->ignore();
		return;
	}

	// Cut first so a dock hiding below cannot put a viewport back into the loop.
	for (const QMetaObject::Connection& connection : m_TabVisibility) disconnect(connection);
	m_TabVisibility.clear();

	for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
		view->SetRenderingEnabled(false);

	m_Renderer->Invoke([&] { m_Renderer->GetGraphics()->WaitIdle(); });

	QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow() { ReleaseRenderResources(); }

void
MainWindow::ReleaseRenderResources() noexcept
{
	ClearPluginPanels();

	if (m_Renderer == nullptr)
		return;

	if (m_ContentExplorer != nullptr)
		m_ContentExplorer->SetThumbnails(nullptr);
	m_Thumbnails.reset();

	ClearFrameStats();
	m_Renderer->Invoke([&] { m_Assets.reset(); });
}

void
MainWindow::NewProject()
{
	const std::optional<editor::NewProjectRequest> request = editor::AskForNewProject(this);
	if (!request)
		return;

	if (!CanClosePluginPanels())
		return;

	// Asked before Create, so declining writes nothing.
	const ProjectOpening opening = AskHowToOpen("New Project", request->projectFile);
	if (opening == ProjectOpening::kCancelled)
		return;

	try
	{
		auto project = assetlib::Project::Create(request->projectFile, request->name);
		if (opening == ProjectOpening::kRestart)
		{
			RestartInto(request->projectFile);
			return;
		}

		SetActiveProject(std::move(project));
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(this, "New Project", e.what());
	}
}

void
MainWindow::OpenProject()
{
	const std::filesystem::path path = editor::AskForProjectToOpen(this);
	if (path.empty())
		return;

	switch (AskHowToOpen("Open Project", path))
	{
	case ProjectOpening::kHere:
		OpenProjectAt(path);
		break;
	case ProjectOpening::kRestart:
		RestartInto(path);
		break;
	case ProjectOpening::kCancelled:
		break;
	}
}

MainWindow::ProjectOpening
MainWindow::AskHowToOpen(const QString& title, const std::filesystem::path& projectFile)
{
	if (!editor::OpeningNeedsRelaunch(
			m_SurfaceShaderDir,
			m_SurfaceCount,
			editor::ShadersDirectoryOf(projectFile)))
	{
		return ProjectOpening::kHere;
	}

	const QMessageBox::StandardButton answer = QMessageBox::question(
		this,
		title,
		QString(
			"%1 requires different shaders from the ones this editor loaded at startup. The "
			"editor will restart to open it.")
			.arg(QString::fromStdWString(projectFile.stem().wstring())),
		QMessageBox::Ok | QMessageBox::Cancel,
		QMessageBox::Ok);

	return answer == QMessageBox::Ok ? ProjectOpening::kRestart : ProjectOpening::kCancelled;
}

void
MainWindow::RestartInto(const std::filesystem::path& projectFile)
{
	m_RelaunchProject = projectFile;
	if (!close())
	{
		m_RelaunchProject.clear();
		return;
	}

	// An open GPU timing window is a window of its own and would keep the application running.
	QCoreApplication::quit();
}

bool
MainWindow::OpenProjectAt(const std::filesystem::path& path)
{
	if (!CanClosePluginPanels())
		return false;

	try
	{
		ZoneScopedN("editor open project");

		SetActiveProject(editor::plugins::OpenProjectWithPlugins(path, *m_Plugins));
		return true;
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(this, "Open Project", e.what());
		return false;
	}
}

void
MainWindow::RefreshTextures()
{
	ZoneScopedN("editor refresh textures");

	if (!m_Project)
		return;

	auto stale = std::vector<std::string>();

	// File I/O, so it belongs on the worker like the prune's scan. No cancel token, so the screen
	// offers no button that would not work.
	const background::TaskResult scanned =
		RunBehindScreen("Open Project", [&](background::Progress& progress) {
			progress.Report(0, 0, "Checking imported sources...");
			stale = m_Project->GetStore().GetStaleImportedTextureSources();
		});

	if (!scanned.Completed())
	{
		// A broken document must not stop a project opening; the asset scan reports it.
		qWarning(
			"Open Project: could not check for changed sources: %s",
			qPrintable(scanned.error));
		return;
	}

	if (stale.empty())
		return;

	auto superseded = QStringList();
	auto moved      = QStringList();
	auto failed     = QStringList();

	// The same cost an import pays, and the same reason it runs off the UI thread.
	const background::TaskResult refreshed = RunBehindScreen(
		"Refresh Textures",
		[&](background::Progress& progress) {
			const assetlib::CancelToken cancel = progress.Cancellation();

			for (size_t i = 0; i < stale.size(); ++i)
			{
				const QString name = QString::fromStdString(stale[i]);
				try
				{
					const assetlib::TextureRefresh result =
						m_Project->GetStore().RefreshImportedTextures(
							stale[i],
							[&](const assetlib::ProgressEvent& event) {
								progress.Report(
									static_cast<int>(event.done),
									static_cast<int>(event.total),
									QString("Compressing %1 (%2 of %3)...")
										.arg(name)
										.arg(event.done + 1)
										.arg(event.total));
							},
							cancel);

					for (const std::string& left : result.superseded)
						superseded << QString::fromStdString(left);

					for (const assetlib::MovedTexture& move : result.moved)
						moved << QString("%1 -> %2")
									 .arg(QString::fromStdString(move.from))
									 .arg(QString::fromStdString(move.to));
				}
				catch (const assetlib::Cancelled&)
				{
					throw;
				}
				catch (const std::exception& e)
				{
					// Each source is a separate group: one failing does not abandon the rest.
					failed << QString("%1: %2").arg(name, QString::fromLatin1(e.what()));
				}
			}
		},
		background::Cancellable::kYes);

	if (refreshed.Cancelled())
		return;

	if (!failed.isEmpty())
	{
		auto problem = QMessageBox(this);
		problem.setWindowTitle("Refresh Textures");
		problem.setIcon(QMessageBox::Warning);
		problem.setText("Some sources could not be re-extracted.");
		problem.setDetailedText(failed.join('\n'));
		problem.exec();
	}

	if (!moved.isEmpty())
	{
		auto followed = QMessageBox(this);
		followed.setWindowTitle("Refresh Textures");
		followed.setIcon(QMessageBox::Information);
		followed.setText("Some textures moved to the name the current rule gives them.");
		followed.setInformativeText(
			"Each held the same bytes as a file the extract wrote, so the materials routing at it "
			"were re-routed and the old file removed. Nothing is drawing anything new.");
		followed.setDetailedText(moved.join('\n'));
		followed.exec();
	}

	if (!superseded.isEmpty())
	{
		// Reported and not acted on -- see docs/asset_containers.md.
		auto left = QMessageBox(this);
		left.setWindowTitle("Refresh Textures");
		left.setIcon(QMessageBox::Information);
		left.setText("Some textures are no longer produced by their source.");
		left.setInformativeText(
			"They are still on disk and nothing has been changed. A material routing at one is "
			"drawing what its source held at import -- re-route it in the Material Editor, or "
			"delete it once nothing does.");
		left.setDetailedText(superseded.join('\n'));
		left.exec();
	}
}

void
MainWindow::UpdateProject()
{
	ZoneScopedN("editor update project");

	if (!m_Project)
		return;

	auto stale  = std::vector<std::string>();
	auto absent = std::vector<std::string>();

	// Header peeks and a dry run, so this is affordable as a project opens -- Migrate's own dry run
	// would re-cook every stale group to answer the same question. Kept as a gate even though the
	// rebuild is no longer offered: Migrate walks and re-saves the whole data root, which a settled
	// project should not pay for on every launch.
	const background::TaskResult scanned =
		RunBehindScreen("Open Project", [&](background::Progress& progress) {
			progress.Report(0, 0, "Checking derived assets...");
			stale = m_Project->GetStore().GetStaleGeometry();
			for (const assetlib::ReimportedSource& source :
		         m_Project->GetStore().Reimport(/*dryRun*/ true).sources)
				absent.insert(absent.end(), source.written.begin(), source.written.end());
		});

	if (!scanned.Completed())
	{
		qWarning("Open Project: could not check the derived assets: %s", qPrintable(scanned.error));
		return;
	}

	if (stale.empty() && absent.empty())
		return;

	auto failed = QStringList();

	const background::TaskResult migrated =
		RunBehindScreen("Update Project", [&](background::Progress& progress) {
			progress.Report(0, 0, "Rebuilding derived assets...");

			// Migrate reports in phases, each with its own count, so the label and the range are
			// taken from the event rather than remembered across them.
			const auto onProgress = [&progress](const assetlib::ProgressEvent& event) {
				progress.Report(
					static_cast<int>(event.done),
					static_cast<int>(event.total),
					editor::startup::RebuildLabel(event));
			};

			for (const assetlib::MigratedFile& file :
		         m_Project->GetStore().Migrate(/*dryRun*/ false, onProgress).files)
				if (file.outcome == assetlib::MigratedFile::Outcome::kFailed)
					failed << QString("%1: %2").arg(
						QString::fromStdString(file.path.filename().string()),
						QString::fromStdString(file.message));
		});

	if (!migrated.Completed())
	{
		qWarning("Update Project: could not rebuild: %s", qPrintable(migrated.error));
		return;
	}

	if (!failed.isEmpty())
	{
		auto problem = QMessageBox(this);
		problem.setWindowTitle("Update Project");
		problem.setIcon(QMessageBox::Warning);
		problem.setText("Some assets could not be rebuilt.");
		problem.setInformativeText(
			"Anything that draws one will report that the project needs updating until its "
			"source is back.");
		problem.setDetailedText(failed.join('\n'));
		problem.exec();
	}
}

void
MainWindow::CleanUnusedTextures()
{
	if (!m_Project)
		return;

	auto scan = assetlib::TexturePruneScan();

	// Scanning parses every .bmaterial in the project, so it runs off the UI thread. It reads assetlib
	// only, never bgl, which is what the loading screen requires of its worker. FindUnusedBakedTextures
	// takes no cancel token, so the screen offers no button that would not work.
	const background::TaskResult scanned = background::RunWithLoadingScreen(
		this,
		"Clean Unused Textures",
		[&](background::Progress& progress) {
			progress.Report(0, 0, "Scanning materials...");

			// The project's store was mounted when the project opened, and a data directory can go
			// away after that -- renamed from a file manager, or on a volume that unmounted. Asked
			// here rather than left to the sweep, which enumerates an absent root as empty and would
			// report a clean project. Inside the worker, so the answer reaches the loading screen's
			// error rather than leaving a Qt slot.
			core::throw_runtime_error_if(
				!std::filesystem::is_directory(m_Project->GetDataDirectory()),
				"the data directory '{}' is not there any more",
				m_Project->GetDataDirectory().string());

			scan = m_Project->GetStore().FindUnusedBakedTextures();
		});

	if (!scanned.Completed())
	{
		QMessageBox::warning(
			this,
			"Clean Unused Textures",
			QString("Could not scan the project:\n\n%1").arg(scanned.error));
		return;
	}

	const auto formatSize = [](uint64_t bytes) {
		return QLocale().formattedDataSize(static_cast<qint64>(bytes));
	};

	if (scan.unused.empty())
	{
		QMessageBox::information(
			this,
			"Clean Unused Textures",
			QString(
				"No unused baked textures.\n\n%1 of the %2 baked textures are referenced by the "
				"project's %3 materials and %4 environment assets.")
				.arg(scan.liveMaps)
				.arg(scan.candidates)
				.arg(scan.materialsScanned)
				.arg(scan.environmentsScanned));
		return;
	}

	auto details = QStringList();
	for (const assetlib::UnusedTexture& texture : scan.unused)
		details << QString::fromStdString(texture.path);

	auto confirm = QMessageBox(this);
	confirm.setWindowTitle("Clean Unused Textures");
	confirm.setIcon(QMessageBox::Warning);
	confirm.setText(QString("Delete %1 unused baked textures?")
	                    .arg(static_cast<qulonglong>(scan.unused.size())));
	confirm.setInformativeText(
		QString(
			"No material in this project references them; %1 will be reclaimed.\n\nThis cannot be "
			"undone, but a deleted map is rebuilt by re-baking the material that needs it.")
			.arg(formatSize(scan.bytes)));
	confirm.setDetailedText(details.join('\n'));

	auto* deleteButton = confirm.addButton("Delete", QMessageBox::DestructiveRole);
	confirm.addButton(QMessageBox::Cancel);
	confirm.setDefaultButton(QMessageBox::Cancel);
	confirm.exec();

	if (confirm.clickedButton() != deleteButton)
		return;

	// Unlinking is fast, so it stays on the UI thread; the scan is what was slow.
	const auto result = m_Project->GetStore().DeleteUnusedBakedTextures(scan);

	if (!result.failed.empty())
	{
		QMessageBox::warning(
			this,
			"Clean Unused Textures",
			QString("Deleted %1 textures, but %2 could not be removed:\n\n%3")
				.arg(static_cast<qulonglong>(result.deleted))
				.arg(static_cast<qulonglong>(result.failed.size()))
				.arg(QString::fromStdString(result.failed.front())));
		return;
	}

	statusBar()->showMessage(
		QString("Deleted %1 unused baked textures, reclaiming %2")
			.arg(static_cast<qulonglong>(result.deleted))
			.arg(formatSize(result.bytes)),
		5000);
}

void
MainWindow::SetActiveProject(assetlib::Project project)
{
	ZoneScopedN("editor set active project");

	ClearPluginPanels();

	if (m_Thumbnails)
		m_Thumbnails->SetStore(nullptr);
	ClearFrameStats();

	// Panel teardown drains render work before the borrowed manager is released on its thread.
	m_Renderer->Invoke([&] { m_Assets.reset(); });

	m_Project = std::make_unique<assetlib::Project>(std::move(project));
	editor::RecordRecentProject(m_RecentProjectsFile, m_Project->GetProjectFile());
	const auto dataDir = QString::fromStdWString(m_Project->GetDataDirectory().wstring());

	// One manager over the editor's one scene: every viewport draws that scene, so a texture a material
	// shares is one upload and one reference count no matter which view shows it. Each view names itself
	// when it places an instance.
	m_Assets = std::make_unique<game::AssetManager>(m_Renderer->GetScene(), m_Project->GetStore());
	m_EditorHost = std::make_unique<editor::plugins::EditorHost>(
		m_Project->GetStore(),
		m_Plugins->Contributions().Catalogs(),
		m_Renderer.get(),
		m_Assets.get(),
		m_Headless,
		editor::plugins::EditorHostDispatch{
			.showPanel = [this](const std::string_view id) { ShowPluginPanel(id); },
			.openAsset = [this](const std::string_view key) { OpenPluginAsset(key); },
			.assetChanged =
				[this](const std::string_view key) {
					m_ContentExplorer->update();
					if (m_Thumbnails != nullptr)
					{
						m_Thumbnails->Invalidate();
						m_Thumbnails->Request(
							QString::fromStdWString(
								m_Project->GetStore().ResolveWritePath(key).wstring()));
					}
					for (const auto& [id, dock] : m_PluginDocks)
					{
						auto* panel = dock.panel.data();
						if (panel == nullptr)
							continue;
						QMetaObject::invokeMethod(
							panel,
							[panel, changed = std::string(key), panelId = id] {
								try
								{
									panel->OnAssetChanged(changed);
								}
								catch (const std::exception& error)
								{
									qWarning(
										"Plugin panel '%s' asset notification failed: %s",
										panelId.c_str(),
										error.what());
								}
								catch (...)
								{
									qWarning(
										"Plugin panel '%s' asset notification failed",
										panelId.c_str());
								}
							},
							Qt::QueuedConnection);
					}
				},
			.viewportCreated = [this](RenderTargetWindow& view) { ConfigureViewport(view); },
		});

	for (const auto id : editor::defaults::c_StartupPanels) ShowPluginPanel(id);
	SetUpFrameStats();

	// Before the explorer roots and the thumbnails paint, so they paint the refreshed textures.
	RefreshTextures();
	UpdateProject();

	// Hand it over before the explorer is rooted: rooting it paints tiles, and each one that misses
	// asks for a render straight away -- a material cannot be resolved without the project store.
	if (m_Thumbnails)
		m_Thumbnails->SetStore(&m_Project->GetStore());

	m_ContentExplorer->SetRootPath(dataDir);

	editor::SetProjectDataRoot(this, dataDir);

	ShowProjectState();

	setWindowTitle(
		editor::WindowTitle(m_InstanceName, QString::fromStdString(m_Project->GetName())));
	statusBar()->showMessage(
		QString("Project data: %1")
			.arg(QString::fromStdString(m_Project->GetDataDirectory().string())));
}

void
MainWindow::SetUpGpuTimingEntry()
{
	// In Window rather than Render: the four entries above it are the docks' own toggles, and this
	// is the one thing here that opens a window of its own -- a graph docked among the viewports
	// would stop the viewport it measures, since only the selected tab renders.
	// Parented, so it goes with the editor; Qt::Window, so it is a window of its own. Hidden until
	// this entry asks for it, and it is what turns timing on while it is up.
	m_GpuTiming = new editor::GpuTimingWindow(this);
	connect(m_GpuTiming, &editor::GpuTimingWindow::TimingWanted, this, [this](bool wanted) {
		if (m_GpuTimingAction == nullptr)
			return;

		// Restore rather than switch off: somebody who had timing on for the log still wants it.
		if (wanted)
			m_GpuTimingWasOn = m_GpuTimingAction->isChecked();

		m_GpuTimingAction->setChecked(wanted || m_GpuTimingWasOn);
	});

	auto* graph = m_Ui.windowMenu->addAction("GPU Timing Graph");
	graph->setCheckable(true);
	graph->setShortcut(QKeySequence("Ctrl+Shift+G"));
	graph->setStatusTip(
		"Graph what each pass of the rendering viewport's frames costs on the GPU, and export it.");

	connect(graph, &QAction::toggled, this, [this](bool shown) {
		m_GpuTiming->setVisible(shown);
		if (shown)
		{
			m_GpuTiming->raise();
			m_GpuTiming->activateWindow();
		}
	});

	// Closed from its own title bar, the entry has to follow: an unchecked box beside a window that
	// is up says the wrong thing, and the next click would then do nothing.
	connect(m_GpuTiming, &editor::GpuTimingWindow::TimingWanted, graph, &QAction::setChecked);
}

void
MainWindow::SetUpPluginsEntry()
{
	m_PluginsWindow = new editor::PluginsWindow(*m_Plugins, this);

	QMenu* plugins = menuBar()->addMenu("Plugins");
	auto*  loaded  = plugins->addAction("Loaded Plugins");
	loaded->setStatusTip("List the plugins this editor loaded at startup.");
	connect(loaded, &QAction::triggered, this, [this] {
		m_PluginsWindow->show();
		m_PluginsWindow->raise();
		m_PluginsWindow->activateWindow();
	});
}

void
MainWindow::SetUpPluginContributions()
{
	const editor::plugins::EditorRegistry& registry = m_Plugins->Contributions();
	if (registry.Menus().empty() && registry.Actions().empty())
		return;

	editor::LanguageResolver language;
	for (const editor::TranslationCatalog& catalog : registry.Catalogs())
		language.RegisterCatalog(catalog);

	core::str::unordered_str_map<QMenu*> menus;
	menus.emplace(editor::c_FileMenuId, m_Ui.fileMenu);
	const auto toolsMenu = [&]() {
		if (m_Ui.toolsMenu == nullptr)
		{
			m_Ui.toolsMenu = new QMenu("Tools", this);
			menuBar()->insertMenu(m_Ui.windowMenu->menuAction(), m_Ui.toolsMenu);
		}
		menus.emplace(editor::c_ToolsMenuId, m_Ui.toolsMenu);
	};

	for (const editor::MenuDesc& desc : registry.Menus())
	{
		QMenu* menu = nullptr;
		if (desc.parentId.empty())
		{
			menu = new QMenu(desc.title.Resolve(language), this);
			menuBar()->insertMenu(m_Ui.windowMenu->menuAction(), menu);
		}
		else
		{
			if (desc.parentId == editor::c_ToolsMenuId)
				toolsMenu();
			menu = menus.at(desc.parentId)->addMenu(desc.title.Resolve(language));
		}
		menus.emplace(desc.id, menu);
	}

	for (const editor::ActionDesc& desc : registry.Actions())
	{
		if (!desc.extensions.empty())
			continue;
		if (desc.menuId == editor::c_ToolsMenuId)
			toolsMenu();
		QMenu*                    menu       = menus.at(desc.menuId);
		QAction*                  action     = menu->addAction(desc.title.Resolve(language));
		const editor::ActionDesc* descriptor = &desc;
		action->setEnabled(false);
		connect(menu, &QMenu::aboutToShow, action, [this, action, descriptor] {
			bool enabled = false;
			try
			{
				enabled =
					m_EditorHost != nullptr && descriptor->action->IsEnabled(*m_EditorHost, {});
			}
			catch (const std::exception& error)
			{
				qWarning("Plugin action predicate failed: %s", error.what());
			}
			action->setEnabled(enabled);
		});
		connect(action, &QAction::triggered, this, [this, descriptor] {
			if (m_EditorHost == nullptr)
				return;
			try
			{
				descriptor->action->Invoke(*m_EditorHost, {});
			}
			catch (const std::exception& error)
			{
				QMessageBox::warning(this, "Plugin Action", error.what());
			}
		});
	}
}

void
MainWindow::ShowPluginPanel(const std::string_view id)
{
	if (m_EditorHost == nullptr)
		return;
	if (const auto found = m_PluginDocks.find(id); found != m_PluginDocks.end())
	{
		found->second.dock->show();
		found->second.dock->raise();
		return;
	}

	const editor::PanelDesc*       desc      = m_Plugins->Contributions().FindPanel(id);
	const editor::AssetEditorDesc* assetDesc = nullptr;
	if (desc == nullptr)
		for (const auto& candidate : m_Plugins->Contributions().AssetEditors())
			if (candidate.id == id)
			{
				assetDesc = &candidate;
				break;
			}
	if (desc == nullptr && assetDesc == nullptr)
		throw std::runtime_error("Editor panel is not registered");
	const auto& title = desc != nullptr ? desc->title : assetDesc->title;
	auto        dockOwner =
		std::make_unique<QDockWidget>(title.Resolve(m_EditorHost->GetLanguageResolver()), this);
	auto* dock = dockOwner.get();
	dock->setObjectName(QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size())));
	editor::EditorPanel* panel = desc != nullptr ? desc->factory->Create(*m_EditorHost, dock) :
	                                               assetDesc->factory->Create(*m_EditorHost, dock);
	if (panel == nullptr || panel->parentWidget() != dock)
	{
		delete panel;
		throw std::runtime_error("Editor panel factory returned an invalid widget");
	}
	dock->setWidget(panel);
	dock->setTitleBarWidget(new QWidget(dock));
	dock->setFeatures(QDockWidget::DockWidgetClosable);
	addDockWidget(Qt::TopDockWidgetArea, dock);
	if (m_EditorDockAnchor != nullptr)
		tabifyDockWidget(m_EditorDockAnchor, dock);
	else
		m_EditorDockAnchor = dock;
	m_Ui.windowMenu->addAction(dock->toggleViewAction());
	connect(dock, &QDockWidget::visibilityChanged, panel, [this, panel](const bool visible) {
		panel->SetActive(editor::IsPanelShown(visible, this));
	});
	m_PluginDocks.emplace(std::string(id), PluginDock{ dock, panel });
	static_cast<void>(dockOwner.release());
	dock->show();
	dock->raise();
}

void
MainWindow::OpenPluginAsset(const std::string_view key)
{
	try
	{
		if (m_EditorHost == nullptr)
			return;
		std::string extension = std::filesystem::path(key).extension().string();
		std::ranges::transform(extension, extension.begin(), [](const unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		const editor::AssetEditorDesc* desc = m_Plugins->Contributions().FindAssetEditor(extension);
		if (desc == nullptr)
			return;

		ShowPluginPanel(desc->id);
		auto* panel =
			dynamic_cast<editor::AssetEditorPanel*>(m_PluginDocks.at(desc->id).panel.data());
		panel->OpenAsset(key);
	}
	catch (const std::exception& error)
	{
		QMessageBox::warning(this, "Plugin Asset", error.what());
	}
	catch (...)
	{
		QMessageBox::warning(this, "Plugin Asset", "The plugin could not open this asset");
	}
}

bool
MainWindow::CanClosePluginPanels()
{
	for (const auto& [id, dock] : m_PluginDocks)
	{
		static_cast<void>(id);
		if (auto* panel = dock.panel.data(); panel != nullptr)
		{
			try
			{
				if (!panel->CanClose())
					return false;
			}
			catch (const std::exception& error)
			{
				QMessageBox::warning(this, "Plugin Panel", error.what());
				return false;
			}
		}
	}
	return true;
}

void
MainWindow::ClearPluginPanels()
{
	for (const auto& [id, dock] : m_PluginDocks)
	{
		delete dock.dock;
		if (dock.panel != nullptr)
		{
			qWarning(
				"Plugin panel '%s' survived dock teardown; deleting it before project services",
				id.c_str());
			delete dock.panel.data();
		}
	}
	m_PluginDocks.clear();
	m_EditorDockAnchor = nullptr;
	m_EditorHost.reset();
}

void
MainWindow::SetUpFrameStats()
{
	if (m_EditorHost == nullptr)
		return;

	m_FrameStats = new QLabel(this);
	m_FrameStats->setObjectName("FrameStats");
	// All three figures describe the current visit: a viewport clears them when it leaves the frame
	// loop, so none of them reaches back to a previous time the tab was up.
	m_FrameStats->setToolTip(
		"The rendering viewport's frame time: mean and worst over the last 120 frames, and how "
		"many frame-start intervals exceeded 20 ms since the tab was selected. "
		"This measures render-loop timing, not missed display refreshes.");

	// A permanent widget sits to the right of the bar and survives showMessage, so the project and
	// texture-cleanup messages cannot overwrite the readout.
	statusBar()->addPermanentWidget(m_FrameStats);

	// One viewport is in the frame loop at a time -- see the dock features above -- so the readout is
	// unambiguously about that one. A hidden viewport stops reporting rather than reporting zero, so
	// the label has to be cleared on the way out: left alone, the tab you just left keeps its last
	// figures on screen and they read as the tab you are now looking at.
	for (QDockWidget* dock : findChildren<QDockWidget*>())
	{
		for (RenderTargetWindow* view : dock->findChildren<RenderTargetWindow*>())
		{
			const QString name = dock->windowTitle();

			// Context is `view`: the connection dies with the viewport it
			// names rather than outliving it holding its pointer.
			m_TabVisibility.push_back(connect(
				dock,
				&QDockWidget::visibilityChanged,
				view,
				[this, view, name](bool visible) {
					if (visible)
					{
						m_FrameStatsSource = view;
						m_FrameStats->setText(editor::FrameStatsText(name, std::nullopt));
						m_GpuTiming->SetSource(name);
					}
					else if (m_FrameStatsSource == view)
					{
						m_FrameStatsSource = nullptr;
						m_FrameStats->clear();
						m_GpuTiming->SetSource(QString());
					}
				}));

			// Queued: FrameStatsUpdated is emitted on the render thread and this touches a widget.
			// Which means a sample can outlive the tab switch that made it stale, hence the source
			// check rather than trusting the emission.
			connect(
				view,
				&RenderTargetWindow::FrameStatsUpdated,
				m_FrameStats,
				[this, view, name](
					double                               meanMs,
					double                               maxMs,
					int                                  slowFrames,
					const std::vector<bgl::PassTimings>& gpuFrames) {
					if (m_FrameStatsSource != view)
						return;

					m_FrameStats->setText(
						editor::FrameStatsText(
							name,
							editor::FrameStats{ .meanMs     = meanMs,
				                                .maxMs      = maxMs,
				                                .slowFrames = slowFrames }));

					m_GpuTiming->AddFrames(gpuFrames);

					// The latest frame, formatted here rather than on the render thread: the log
					// wants one frame as a table and the graph wants every frame as numbers, and
					// formatting at the source would make them two copies of the same rows.
					if (m_LogNextPassTimings && !gpuFrames.empty())
					{
						m_LogNextPassTimings = false;
						qInfo().noquote() << "GPU pass timings," << name << "\n"
										  << editor::PassTimingsText(gpuFrames.back().passes);
					}
				},
				Qt::QueuedConnection);
		}
	}
}

void
MainWindow::ShowProjectState()
{
	m_EditorDockAnchor->show();
	m_ContentExplorerDock->show();
	m_EditorDockAnchor->raise();

	resizeDocks({ m_EditorDockAnchor, m_ContentExplorerDock }, { 700, 220 }, Qt::Vertical);
}

void
MainWindow::ConfigureViewport(RenderTargetWindow& view)
{
	if (m_TaaOverride)
		view.SetTaaEnabled(*m_TaaOverride);
	if (m_RenderScaleOverride)
		view.SetRenderScale(*m_RenderScaleOverride);
	if (m_ReconstructionWidthOverride)
		view.SetTaaReconstructionWidth(*m_ReconstructionWidthOverride);
	if (m_BloomOverride)
		view.SetBloomEnabled(*m_BloomOverride);
	if (m_ColorGradeOverride)
		view.SetColorGradeEnabled(*m_ColorGradeOverride);
	view.SetOutlineEnabled(m_OutlineEnabled);
	view.SetGpuTimingEnabled(m_GpuTimingAction != nullptr && m_GpuTimingAction->isChecked());
}

void
MainWindow::ClearFrameStats() noexcept
{
	for (const QMetaObject::Connection& connection : m_TabVisibility) disconnect(connection);
	m_TabVisibility.clear();
	m_FrameStatsSource = nullptr;
	delete m_FrameStats;
	m_FrameStats = nullptr;
	if (m_GpuTiming != nullptr)
		m_GpuTiming->SetSource(QString());
}
