#include "MainWindow.h"

#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>

#include "Async/BackgroundTask.h"
#include "Project/Project.h"
#include "Render/Renderer.h"
#include "Thumbnails/AssetThumbnailCache.h"
#include "Windows/AnimationEditor/AnimationEditorWindow.h"
#include "Windows/AnimationEditor/AnimationPreviewWindow.h"
#include "Windows/ContentExplorer/ContentExplorerWindow.h"
#include "Windows/LevelEditor/LevelEditorWindow.h"
#include "Windows/MaterialEditor/MaterialEditorWindow.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "util/window_title.h"

#include <QActionGroup>
#include <QMenuBar>
#include <assetlib/AssetStore.h>
#include <assetlib/texture_prune.h>
#include <bgl/IGraphics.h>
#include <core/err/util.h>
#include <core/file/file.h>
#include <core/platform/util.h>
#include <core/settings/Settings.h>
#include <gamelib/AssetManager.h>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
	try
	{
		Build();
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
MainWindow::Build()
{
	m_Ui.setupUi(this);

	connect(m_Ui.actionNewProject, &QAction::triggered, this, &MainWindow::NewProject);
	connect(m_Ui.actionOpenProject, &QAction::triggered, this, &MainWindow::OpenProject);
	connect(
		m_Ui.actionCleanUnusedTextures,
		&QAction::triggered,
		this,
		&MainWindow::CleanUnusedTextures);
	connect(m_Ui.actionExit, &QAction::triggered, this, &QWidget::close);

	std::string startupProject;
	{
		const auto     configPath = core::file::get_executable_path().parent_path() / "config.json";
		core::Settings settings(configPath);

		startupProject = settings["startupProject"].GetOrDefault(std::string());
		m_InstanceName =
			QString::fromStdString(settings["instanceName"].GetOrDefault(std::string()));

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

		// The editor's one Scene. Every viewport (the Level Editor, the Material Editor's model
		// preview) renders it through a SceneView of its own, so geometry, textures and materials
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

		// The renderer owns the Graphics and the Scene and, once threaded, is the only thing that
		// touches them. Every viewport and the thumbnail cache render through it.
		m_Renderer = std::make_unique<Renderer>(gfxOpts, sceneDesc);

		auto levelDesc             = RenderTargetWindowDesc();
		levelDesc.renderer         = m_Renderer.get();
		levelDesc.initialInstances = settings["levelEditor"]["initialInstances"].GetOrDefault(1000);

		// Per viewport, not graphics-wide: it sizes what this window's render target allocates, the
		// way initialInstances above sizes its instance buffer. The thumbnail cache is not offered
		// it -- it renders single frames, loading hashed alpha as the blend it converges to.
		levelDesc.taaEnabled = settings["levelEditor"]["temporalAA"].GetOrDefault(true);

		// A starting density, so a machine that must always reproduce another display's can say so
		// once. The Render menu moves every viewport from here.
		levelDesc.renderScale = settings["levelEditor"]["renderScale"].GetOrDefault(1.0f);

		// What the resolve reconstructs a scaled frame with. Beside the scale because it is only
		// legible against one: at scale 1 it changes nothing.
		levelDesc.taaReconstructionWidth =
			settings["levelEditor"]["taaReconstructionWidth"].GetOrDefault(0.4f);

		auto levelEnv = LevelEditorEnv();
		levelEnv.environmentMap =
			settings["levelEditor"]["environmentMap"].GetOrDefault(std::string());
		levelEnv.dataRoot = settings["levelEditor"]["dataRoot"].GetOrDefault(std::string());

		// Absent, and the .benv's own exposure stands -- which is the correct one for its maps.
		if (auto exposure = settings["levelEditor"]["exposure"])
			levelEnv.exposureOverride = exposure.GetOrDefault(1.0f);

		m_LevelEditor = new LevelEditorWindow(this, std::move(levelDesc), std::move(levelEnv));

		auto matSettings                = settings["materialEditor"];
		auto matDesc                    = MaterialEditorWindowDesc();
		matDesc.renderer                = m_Renderer.get();
		matDesc.initialPreviewInstances = matSettings["initialPreviewInstances"].GetOrDefault(16u);
		matDesc.taaEnabled              = matSettings["temporalAA"].GetOrDefault(true);
		matDesc.renderScale             = matSettings["renderScale"].GetOrDefault(1.0f);
		matDesc.taaReconstructionWidth  = matSettings["taaReconstructionWidth"].GetOrDefault(0.4f);
		matDesc.previewEnv.environmentMap =
			matSettings["environmentMap"].GetOrDefault(std::string());
		matDesc.previewEnv.dataRoot = matSettings["dataRoot"].GetOrDefault(std::string());

		// Absent, and the .benv's own derived exposure stands -- which is the correct one for its maps.
		if (auto exposure = matSettings["exposure"])
			matDesc.previewEnv.exposureOverride = exposure.GetOrDefault(1.0f);

		auto thumbSettings         = settings["thumbnails"];
		auto thumbDesc             = AssetThumbnailDesc();
		thumbDesc.renderer         = m_Renderer.get();
		thumbDesc.dimension        = thumbSettings["dimension"].GetOrDefault(256u);
		thumbDesc.initialInstances = thumbSettings["initialInstances"].GetOrDefault(256u);
		thumbDesc.environmentMap   = thumbSettings["environmentMap"].GetOrDefault(std::string());
		thumbDesc.dataRoot         = thumbSettings["dataRoot"].GetOrDefault(std::string());

		if (auto exposure = thumbSettings["exposure"])
			thumbDesc.exposureOverride = exposure.GetOrDefault(1.0f);

		auto animSettings = settings["animationEditor"];
		auto animDesc     = AnimationEditorWindowDesc();
		animDesc.renderer = m_Renderer.get();
		animDesc.initialPreviewInstances =
			animSettings["initialPreviewInstances"].GetOrDefault(16u);
		animDesc.taaEnabled             = animSettings["temporalAA"].GetOrDefault(true);
		animDesc.renderScale            = animSettings["renderScale"].GetOrDefault(1.0f);
		animDesc.taaReconstructionWidth = animSettings["taaReconstructionWidth"].GetOrDefault(0.4f);
		// Falls back to the material editor's environment: both are asset previews wanting the
		// same neutral look, and a config predating this panel would otherwise light it with
		// nothing -- which draws black and says nothing.
		animDesc.previewEnv.environmentMap = animSettings["environmentMap"].GetOrDefault(
			matSettings["environmentMap"].GetOrDefault(std::string()));
		animDesc.previewEnv.dataRoot = animSettings["dataRoot"].GetOrDefault(
			matSettings["dataRoot"].GetOrDefault(std::string()));

		// Absent, and the .benv's own derived exposure stands -- which is the correct one for its maps.
		if (auto exposure = animSettings["exposure"])
			animDesc.previewEnv.exposureOverride = exposure.GetOrDefault(1.0f);

		m_MaterialEditor  = new MaterialEditorWindow(this, std::move(matDesc));
		m_AnimationEditor = new AnimationEditorWindow(this, std::move(animDesc));
		m_Thumbnails      = std::make_unique<AssetThumbnailCache>(std::move(thumbDesc));
	}

	setDockNestingEnabled(true);
	setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

	m_LevelEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_LevelEditor->setMinimumSize(256, 256);

	m_LevelEditorDock = new QDockWidget("Level Editor", this);
	m_LevelEditorDock->setObjectName("LevelEditorDock");
	m_LevelEditorDock->setWidget(m_LevelEditor);
	m_LevelEditorDock->setTitleBarWidget(new QWidget(m_LevelEditorDock));
	addDockWidget(Qt::TopDockWidgetArea, m_LevelEditorDock);

	m_MaterialEditorDock = new QDockWidget("Material Editor", this);
	m_MaterialEditorDock->setObjectName("MaterialEditorDock");
	m_MaterialEditorDock->setWidget(m_MaterialEditor);
	m_MaterialEditorDock->setTitleBarWidget(new QWidget(m_MaterialEditorDock));
	addDockWidget(Qt::TopDockWidgetArea, m_MaterialEditorDock);

	tabifyDockWidget(m_LevelEditorDock, m_MaterialEditorDock);

	m_AnimationEditorDock = new QDockWidget("Animation Editor", this);
	m_AnimationEditorDock->setObjectName("AnimationEditorDock");
	m_AnimationEditorDock->setWidget(m_AnimationEditor);
	m_AnimationEditorDock->setTitleBarWidget(new QWidget(m_AnimationEditorDock));
	addDockWidget(Qt::TopDockWidgetArea, m_AnimationEditorDock);

	tabifyDockWidget(m_MaterialEditorDock, m_AnimationEditorDock);

	m_ContentExplorerDock = new QDockWidget("Content Explorer", this);
	m_ContentExplorerDock->setObjectName("ContentExplorerDock");

	// Not floatable: its title bar sits directly under the separator the user drags to make it
	// taller, and a grab that lands low would otherwise tear it out into a window of its own -- which
	// on macOS is a separate top-level that goes behind the editor.
	m_ContentExplorerDock->setFeatures(
		QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);

	// The explorer refuses to delete what a panel still holds: a material the Material Editor has
	// open (its next Save would write it straight back), and the mesh and clip files the Animation
	// panel is offering. Asked at each deletion, so there is no copy of the answer to go stale.
	m_ContentExplorer = new ContentExplorerWindow(m_ContentExplorerDock, [this] {
		auto held = m_MaterialEditor->OpenMaterialPaths();
		held += m_AnimationEditor->HeldOpenPaths();
		return held;
	});
	m_ContentExplorer->SetThumbnails(m_Thumbnails.get());

	// Baking rewrites the material on disk, which is where the Material Editor's panel reads the
	// staleness marker and the baked-texture listing from. The Animation panel's Bake Now goes the
	// same way.
	connect(
		m_ContentExplorer,
		&ContentExplorerWindow::MaterialBaked,
		m_MaterialEditor,
		&MaterialEditorWindow::RefreshMaterialState);
	for (auto* preview : m_AnimationEditor->findChildren<AnimationPreviewWindow*>())
		connect(
			preview,
			&AnimationPreviewWindow::MaterialBaked,
			m_MaterialEditor,
			&MaterialEditorWindow::RefreshMaterialState);

	m_ContentExplorer->setMinimumSize(0, 0);
	m_ContentExplorerDock->setWidget(m_ContentExplorer);
	addDockWidget(Qt::BottomDockWidgetArea, m_ContentExplorerDock);

	DriveViewportsFromTab(m_LevelEditorDock);
	DriveViewportsFromTab(m_MaterialEditorDock);
	DriveViewportsFromTab(m_AnimationEditorDock);

	// Leaving the Animation tab closes what it was showing, releasing its acquisitions and every
	// held-open path. visibilityChanged, not hideEvent: a tabified dock's widget gets no hideEvent
	// on a tab switch.
	m_TabVisibility.push_back(connect(
		m_AnimationEditorDock,
		&QDockWidget::visibilityChanged,
		m_AnimationEditor,
		&AnimationEditorWindow::SetDockVisible));

	// The Material tab the same way, back to the default sphere. Unsaved graph edits go with it,
	// and nothing asks: the panel writes only on Save.
	m_TabVisibility.push_back(connect(
		m_MaterialEditorDock,
		&QDockWidget::visibilityChanged,
		m_MaterialEditor,
		&MaterialEditorWindow::SetDockVisible));

	m_Ui.menuWindow->addAction(m_LevelEditorDock->toggleViewAction());
	m_Ui.menuWindow->addAction(m_MaterialEditorDock->toggleViewAction());
	m_Ui.menuWindow->addAction(m_AnimationEditorDock->toggleViewAction());
	m_Ui.menuWindow->addAction(m_ContentExplorerDock->toggleViewAction());

	SetUpRenderMenu();

	SetUpFrameStats();

	// config.json may name a project to open on launch, so working on one does not mean reopening
	// it every run. It is machine-local (the file is git-ignored), which is what makes naming an
	// absolute path in it reasonable.
	if (startupProject.empty() || !OpenProjectAt(core::expand_home(startupProject)))
		ShowEmptyState();
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

	connect(taa, &QAction::toggled, this, [this](bool enabled) {
		for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
			view->SetTaaEnabled(enabled);
	});

	auto* outline = render->addAction("Selection Outline");
	outline->setCheckable(true);
	outline->setChecked(true);
	outline->setStatusTip("Contour the selected submesh in the viewports.");

	connect(outline, &QAction::toggled, this, [this](bool enabled) {
		for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
			view->SetOutlineEnabled(enabled);
	});

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

		connect(action, &QAction::triggered, this, [this, factor]() {
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

		connect(action, &QAction::triggered, this, [this, value]() {
			for (RenderTargetWindow* view : findChildren<RenderTargetWindow*>())
				view->SetTaaReconstructionWidth(value);
		});
	}
}

void
MainWindow::closeEvent(QCloseEvent* event)
{
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
	if (m_Renderer == nullptr)
		return;

	if (m_ContentExplorer != nullptr)
		m_ContentExplorer->SetThumbnails(nullptr);
	m_Thumbnails.reset();

	// Its acquisitions release through the manager, so they go before m_Assets does.
	if (m_AnimationEditor != nullptr)
		m_AnimationEditor->SetAssets(nullptr);

	// After the thumbnails, which release their materials back through it, and before the viewports,
	// so the instances it deletes leave views that are still standing.
	m_Renderer->Invoke([&] { m_Assets.reset(); });

	delete m_LevelEditor;
	m_LevelEditor = nullptr;

	delete m_MaterialEditor;
	m_MaterialEditor = nullptr;

	delete m_AnimationEditor;
	m_AnimationEditor = nullptr;
}

void
MainWindow::NewProject()
{
	const auto name = QInputDialog::getText(this, "New Project", "Project name:").trimmed();
	if (name.isEmpty())
		return;

	const auto location = QFileDialog::getExistingDirectory(this, "Select Project Location");
	if (location.isEmpty())
		return;

	const auto root        = std::filesystem::path(location.toStdWString()) / name.toStdString();
	const auto projectFile = root / (name.toStdString() + Project::c_FileExtension);

	try
	{
		SetActiveProject(Project::Create(projectFile, name.toStdString()));
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(this, "New Project", e.what());
	}
}

void
MainWindow::OpenProject()
{
	const auto filter =
		QString("Bernini Project (*%1)").arg(QString::fromUtf8(Project::c_FileExtension));
	const auto file = QFileDialog::getOpenFileName(this, "Open Project", QString(), filter);
	if (file.isEmpty())
		return;

	OpenProjectAt(std::filesystem::path(file.toStdWString()));
}

bool
MainWindow::OpenProjectAt(const std::filesystem::path& path)
{
	try
	{
		SetActiveProject(Project::Open(path));
		return true;
	}
	catch (const std::exception& e)
	{
		QMessageBox::warning(this, "Open Project", e.what());
		return false;
	}
}

void
MainWindow::CleanUnusedTextures()
{
	if (!m_Project)
		return;

	auto scan = assetlib::TexturePruneScan();

	// Scanning parses every .bmaterial in the project, so it runs off the UI thread. It reads assetlib
	// only, never bgl, which is what the loading screen requires of its worker. findUnusedBakedTextures
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

			scan = assetlib::findUnusedBakedTextures(m_Project->GetStore());
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
	const auto result = assetlib::deleteUnusedBakedTextures(scan, m_Project->GetStore());

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
MainWindow::SetActiveProject(Project project)
{
	m_Project = std::make_unique<Project>(std::move(project));

	const auto dataDir = QString::fromStdWString(m_Project->GetDataDirectory().wstring());

	// A manager resolves every path against one Data root, so a new project needs a new one. The
	// consumers below borrow it, so it has to be replaced before any of them are told about it.
	if (m_Thumbnails)
		m_Thumbnails->SetAssets(nullptr);
	if (m_AnimationEditor)
		m_AnimationEditor->SetAssets(nullptr);

	// ~AssetManager hands every asset it still holds back to the scene, so it runs on the render
	// thread like any other scene mutation -- the viewports are still drawing at this point.
	m_Renderer->Invoke([&] { m_Assets.reset(); });

	// One manager over the editor's one scene: every viewport draws that scene, so a texture a material
	// shares is one upload and one reference count no matter which view shows it. Each view names itself
	// when it places an instance.
	m_Assets = std::make_unique<game::AssetManager>(m_Renderer->GetScene(), m_Project->GetStore());

	// Hand it over before the explorer is rooted: rooting it paints tiles, and each one that misses
	// asks for a render straight away -- a material cannot be resolved without a manager.
	if (m_Thumbnails)
		m_Thumbnails->SetAssets(m_Assets.get());

	m_ContentExplorer->SetRootPath(dataDir);

	if (m_MaterialEditor)
	{
		// Root first, then reset: the reset repopulates the preview, which resolves the material
		// paths it finds against the data root.
		m_MaterialEditor->SetDataRoot(dataDir);
		m_MaterialEditor->Reset();
	}

	if (m_AnimationEditor)
	{
		m_AnimationEditor->SetDataRoot(dataDir);
		m_AnimationEditor->SetAssets(m_Assets.get());
	}

	ShowProjectState();

	setWindowTitle(
		editor::WindowTitle(m_InstanceName, QString::fromStdString(m_Project->GetName())));
	statusBar()->showMessage(
		QString("Project data: %1")
			.arg(QString::fromStdString(m_Project->GetDataDirectory().string())));
}

void
MainWindow::DriveViewportsFromTab(QDockWidget* dock)
{
	// Tabifying leaves the unselected dock's widget visible to Qt -- it is stacked behind, not
	// hidden -- so without this every viewport in the editor keeps drawing whatever tab is on top.
	// visibilityChanged is the signal that follows the tab, which show/hideEvent do not.
	//
	// One connection per view, so each dies with the view it drives; closeEvent cuts them all before
	// that, because a dock hiding would otherwise put a viewport back into the loop on the way out.
	for (RenderTargetWindow* view : dock->findChildren<RenderTargetWindow*>())
	{
		m_TabVisibility.push_back(
			connect(dock, &QDockWidget::visibilityChanged, view, [view](bool visible) {
				view->SetRenderingEnabled(visible);
			}));
	}
}

void
MainWindow::SetUpFrameStats()
{
	if (m_LevelEditor == nullptr)
		return;

	m_FrameStats = new QLabel(this);
	m_FrameStats->setObjectName("FrameStats");
	// The count is cumulative where the two times are windowed: a session total, not a property of
	// the 120 frames beside it.
	m_FrameStats->setToolTip(
		"Level Editor frame time: mean and worst over the last 120 frames, and how many frames "
		"have overrun a vblank since startup.");

	// A permanent widget sits to the right of the bar and survives showMessage, so the project and
	// texture-cleanup messages cannot overwrite the readout.
	statusBar()->addPermanentWidget(m_FrameStats);

	// Queued: FrameStatsUpdated is emitted on the render thread and this touches a widget.
	connect(
		m_LevelEditor,
		&RenderTargetWindow::FrameStatsUpdated,
		this,
		[this](double meanMs, double maxMs, int missed) {
			m_FrameStats->setText(
				QString::asprintf(
					"frame %.1f ms avg  %.1f ms max  %d missed",
					meanMs,
					maxMs,
					missed));
		},
		Qt::QueuedConnection);
}

void
MainWindow::ShowEmptyState()
{
	setWindowTitle(editor::WindowTitle(m_InstanceName, QString()));

	m_LevelEditorDock->hide();
	m_MaterialEditorDock->hide();
	m_AnimationEditorDock->hide();
	m_ContentExplorerDock->hide();

	m_Ui.actionSave->setEnabled(false);
	m_Ui.actionCleanUnusedTextures->setEnabled(false);
	m_Ui.menuEdit->setEnabled(false);
	m_Ui.menuWindow->setEnabled(false);

	auto* placeholder = new QLabel(
		"Open a project to get started.\n\nFile ▸ New Project…   or   File ▸ Open Project…",
		this);
	placeholder->setObjectName("EmptyStatePlaceholder");
	placeholder->setAlignment(Qt::AlignCenter);
	placeholder->setEnabled(false);

	setCentralWidget(placeholder);
}

void
MainWindow::ShowProjectState()
{
	setCentralWidget(nullptr);

	m_LevelEditorDock->show();
	m_MaterialEditorDock->show();
	m_AnimationEditorDock->show();
	m_ContentExplorerDock->show();
	m_LevelEditorDock->raise();

	m_Ui.actionSave->setEnabled(true);
	m_Ui.actionCleanUnusedTextures->setEnabled(true);
	m_Ui.menuEdit->setEnabled(true);
	m_Ui.menuWindow->setEnabled(true);

	resizeDocks({ m_LevelEditorDock, m_ContentExplorerDock }, { 700, 220 }, Qt::Vertical);
}
