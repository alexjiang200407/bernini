#include "MainWindow.h"
#include "Plugins/plugin_loader.h"

#include "Windows/AnimationEditor/AnimationEditorWindow.h"
#include "Windows/AnimationEditor/GroundControls.h"
#include "Windows/AnimationEditor/Scrubber.h"
#include "Windows/BlendSpaceEditor/BlendSpaceEditorWindow.h"
#include "Windows/ContentExplorer/ContentExplorerWindow.h"
#include "Windows/GpuTiming/GpuTimingWindow.h"
#include "Windows/MaterialEditor/MaterialEditorWindow.h"
#include "Windows/MaterialEditor/MaterialPreviewWindow.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "util/QtSupport.h"  // IWYU pragma: keep
#include "util/follows_project.h"
#include "util/recent_projects.h"
#include "util/rig_containers.h"
#include <algorithm>
#include <array>
#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/blend.h>
#include <assetlib/project_layout.h>
#include <bgl/GeomHandle.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MaterialHandle.h>
#include <cstdint>
#include <editor_plugin_api/EditorPanel.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_plugin_api/PluginDescriptor.h>
#include <gamelib/AssetManager.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QModelIndex>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtTest/qtestmouse.h>
#include <assetlib_structs/BMaterial.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/file/file.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <qcontainerfwd.h>
#include <qlist.h>
#include <qmainwindow.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <qtmetamacros.h>
#include <qwidget.h>
#include <string>
#include <type_traits>
#include <vector>

// What a viewport's `headless` flag buys: a whole editor -- device, renderer, every viewport --
// standing in a test, so its construction and teardown are pinned rather than found by driving the
// app. What it draws is still bgl_extended_tests' job.

namespace
{
	namespace fs = std::filesystem;

	// The panels that own a viewport today. The count below is what makes another one loud.
	constexpr int c_ViewportCount = 3;

	/** A scaffolded project and a headless config.json, both in a directory of their own. */
	struct HeadlessEditor
	{
		QTemporaryDir temp;

		HeadlessEditor()
		{
			assetlib::Project::Create(ProjectFile(), "MyGame");

			// Written here rather than into the deployed config.json, which editor_tests shares
			// with the editor binary it is built beside.
			//
			// temporalAA off in both: a history costs a viewport nothing to skip here, and
			// these cases are about what is built, not what it accumulates.
			const std::string config = R"({
  "headless": true,
  "materialEditor":  { "temporalAA": false },
  "animationEditor": { "temporalAA": false }
})";

			core::file::write_atomic(ConfigFile(), config);
		}

		[[nodiscard]] fs::path
		Root() const
		{
			return temp.path().toStdString() / fs::path("MyGame");
		}

		[[nodiscard]] fs::path
		ProjectFile() const
		{
			return Root() / ("MyGame" + std::string(assetlib::Project::c_FileExtension));
		}

		[[nodiscard]] fs::path
		ConfigFile() const
		{
			return temp.path().toStdString() / fs::path("config.json");
		}

		[[nodiscard]] fs::path
		DataRoot() const
		{
			return Root() / "Data";
		}

		[[nodiscard]] assetlib::Project
		Open() const
		{
			return assetlib::Project::Open(ProjectFile());
		}

		// What main() does before the window: the directories the config names, and no others.
		[[nodiscard]] std::unique_ptr<editor::plugins::PluginSession>
		Plugins() const
		{
			return std::make_unique<editor::plugins::PluginSession>(
				editor::plugins::PluginSession::Load(
					editor::plugins::ConfiguredPluginDirectories(ConfigFile()),
					editor::plugins::CurrentBuildIdentity(),
					temp.path().toStdString() / fs::path("plugin-copies")));
		}
	};

#if defined(EDITOR_PLUGIN_FIXTURE)
	struct PluginHeadlessEditor : HeadlessEditor
	{
		PluginHeadlessEditor()
		{
			const fs::path pluginDirectory = temp.path().toStdString() / fs::path("fixture-plugin");
			fs::create_directories(pluginDirectory);
			const fs::path source(EDITOR_PLUGIN_FIXTURE);
			const fs::path module = source.filename();
			fs::copy_file(source, pluginDirectory / module, fs::copy_options::overwrite_existing);
			const editor::plugins::BuildIdentity build = editor::plugins::CurrentBuildIdentity();
			std::ofstream(pluginDirectory / editor::c_PluginDescriptorFileName)
				<< nlohmann::json{
					   { "version", editor::c_PluginDescriptorVersion },
					   { "id", "sample.fixture" },
					   { "engineBuildId", build.id },
					   { "configuration", build.configuration },
					   { "editor", module.generic_string() },
					   { "dependencies", nlohmann::json::array() },
				   }
					   .dump(2);
			std::ofstream(ProjectFile())
				<< nlohmann::json{
					   { "name", "MyGame" },
					   { "version", 1 },
					   { "plugins", { "sample.fixture" } },
				   }
					   .dump(2);
			std::ofstream(ConfigFile())
				<< nlohmann::json{
					   { "headless", true },
					   { "startupProject", ProjectFile().string() },
					   { "pluginDirectories", { pluginDirectory.string() } },
					   { "materialEditor", { { "temporalAA", false } } },
					   { "animationEditor", { { "temporalAA", false } } },
				   }
					   .dump(2);
		}
	};
#endif

	[[nodiscard]] QAction*
	ActionNamed(const MainWindow& window, const QString& text)
	{
		const QList<QAction*> actions = window.findChildren<QAction*>();
		const auto            named =
			std::ranges::find_if(actions, [&text](const QAction* a) { return a->text() == text; });

		return named == actions.end() ? nullptr : *named;
	}

	void
	ObserveViewportTeardown(MainWindow& window, QObject& observer, std::vector<fs::path>& roots)
	{
		for (auto* view : window.findChildren<RenderTargetWindow*>())
		{
			game::AssetManager* assets = nullptr;
			view->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef&) {
				assets = &context.assets;
			});
			QObject::connect(view, &QObject::destroyed, &observer, [assets, &roots] {
				roots.push_back(assets->GetStore().GetDataRoot());
			});
		}
	}

	/** A panel nobody listed anywhere, to prove the walk finds one. */
	class SpyPanel : public QObject, public editor::IFollowsProject
	{
	public:
		using QObject::QObject;

		void
		SetDataRoot(const QString& dataRoot) override
		{
			root = dataRoot;
			++calls;
		}

		QString root;
		int     calls = 0;
	};
}

#if defined(EDITOR_PLUGIN_FIXTURE)
TEST_CASE("A loaded plugin panel is owned by one project host", "[mainwindow][plugins][render]")
{
	const PluginHeadlessEditor    editor;
	QPointer<editor::EditorPanel> panel;
	{
		auto window =
			std::make_unique<MainWindow>(editor.Plugins(), editor.Open(), editor.ConfigFile());
		QMenu* tools = nullptr;
		for (QAction* action : window->menuBar()->actions())
			if (action->menu() != nullptr && action->text() == "Tools")
				tools = action->menu();
		REQUIRE(tools != nullptr);
		Q_EMIT tools->aboutToShow();
		QAction* show = ActionNamed(*window, "Fixture Panel");
		REQUIRE(show != nullptr);
		REQUIRE(show->isEnabled());
		show->trigger();
		for (QWidget* widget : window->findChildren<QWidget*>())
			if (auto* candidate = dynamic_cast<editor::EditorPanel*>(widget))
				panel = candidate;
		REQUIRE(panel != nullptr);
		CHECK(panel->parentWidget()->objectName() == "sample.fixture_panel");
		SECTION("Normal Qt ownership") {}
		SECTION("A reparented panel is reclaimed before its host dies")
		{
			panel->setParent(nullptr);
		}
	}
	CHECK(panel.isNull());
}

TEST_CASE(
	"Asset changes reach only surviving project panels",
	"[mainwindow][plugins][render][notification]")
{
	const PluginHeadlessEditor editor;
	auto                       window =
		std::make_unique<MainWindow>(editor.Plugins(), editor.Open(), editor.ConfigFile());
	const auto open = [&window](const QString& id) {
		for (QAction* action : window->menuBar()->actions())
			if (action->text() == "Tools" && action->menu() != nullptr)
				Q_EMIT action->menu()->aboutToShow();
		auto* action = ActionNamed(*window, id);
		REQUIRE(action != nullptr);
		action->trigger();
		auto* dock = window->findChild<QDockWidget*>(id);
		REQUIRE(dock != nullptr);
		return dock->widget();
	};
	QPointer<QWidget> first   = open("sample.observer_one");
	QPointer<QWidget> second  = open("sample.observer_two");
	auto*             publish = first->findChild<QPushButton*>();
	REQUIRE(publish != nullptr);
	auto* key = first->findChild<QLineEdit*>();
	REQUIRE(key != nullptr);
	key->setText("Authored/original.bfixture");
	publish->click();
	key->setText("Authored/replaced.bfixture");
	CHECK(first->property("notificationCount").toInt() == 0);
	CHECK(second->property("notificationCount").toInt() == 0);

	SECTION("Inactive panels receive the owned key once")
	{
		first->hide();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		for (const auto& panel : { first, second })
		{
			CHECK(panel->property("notificationCount").toInt() == 1);
			CHECK(panel->findChild<QLabel*>()->text() == "Authored/original.bfixture");
		}
		publish->click();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		CHECK(first->property("notificationCount").toInt() == 2);
		CHECK(second->findChild<QLabel*>()->text() == "Authored/replaced.bfixture");
	}
	SECTION("A panel created after publication receives no earlier change")
	{
		auto* late = open("sample.observer_late");
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		CHECK(first->property("notificationCount").toInt() == 1);
		CHECK(late->property("notificationCount").toInt() == 0);
	}
	SECTION("A destroyed recipient is skipped")
	{
		delete second.data();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		CHECK(second.isNull());
		CHECK(first->property("notificationCount").toInt() == 1);
	}
	SECTION("A throwing recipient does not suppress other panels")
	{
		first->setProperty("throwOnChange", true);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		CHECK(first->property("notificationCount").toInt() == 0);
		CHECK(second->property("notificationCount").toInt() == 1);
	}
	SECTION("A new project host receives no queued changes from its predecessor")
	{
		window.reset();
		REQUIRE(first.isNull());
		REQUIRE(second.isNull());
		window = std::make_unique<MainWindow>(editor.Plugins(), editor.Open(), editor.ConfigFile());
		auto* next = open("sample.observer_one");
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
		CHECK(next->property("notificationCount").toInt() == 0);
	}
}

TEST_CASE(
	"Lazy plugin viewports preserve defaults and follow render choices",
	"[mainwindow][plugins][render]")
{
	const PluginHeadlessEditor editor;
	MainWindow                 window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	bool                       overrideDefaults = false;
	SECTION("Plugin presentation defaults") {}
	SECTION("Earlier menu choices override plugin defaults")
	{
		overrideDefaults = true;
		auto* scale      = ActionNamed(window, "0.5x");
		auto* width      = ActionNamed(window, "0.8 px");
		REQUIRE(scale != nullptr);
		REQUIRE(width != nullptr);
		scale->trigger();
		width->trigger();
	}
	auto* taa = ActionNamed(window, "Temporal Antialiasing");
	REQUIRE(taa != nullptr);
	CHECK_FALSE(taa->isEnabled());
	for (QAction* action : window.menuBar()->actions())
		if (action->text() == "Tools" && action->menu() != nullptr)
			Q_EMIT action->menu()->aboutToShow();
	auto* show = ActionNamed(window, "Fixture Panel");
	REQUIRE(show != nullptr);
	show->trigger();
	auto* dock = window.findChild<QDockWidget*>("sample.fixture_panel");
	REQUIRE(dock != nullptr);
	auto* viewport = dock->findChild<RenderTargetWindow*>();
	REQUIRE(viewport != nullptr);
	CHECK(viewport->GetRenderScale() == Catch::Approx(overrideDefaults ? 0.5f : 0.75f));
	CHECK(viewport->GetTaaReconstructionWidth() == Catch::Approx(overrideDefaults ? 0.8f : 0.6f));
	QMenu* render = nullptr;
	for (QAction* action : window.menuBar()->actions())
		if (action->text() == "Render")
			render = action->menu();
	REQUIRE(render != nullptr);
	Q_EMIT render->aboutToShow();
	CHECK(taa->isEnabled());
	CHECK(taa->isChecked());
	auto* scale = ActionNamed(window, "1.5x");
	REQUIRE(scale != nullptr);
	scale->trigger();
	CHECK(viewport->GetRenderScale() == Catch::Approx(1.5f));
}

TEST_CASE("A plugin editor failure stays inside the GUI boundary", "[mainwindow][plugins]")
{
	const PluginHeadlessEditor editor;
	auto                       window =
		std::make_unique<MainWindow>(editor.Plugins(), editor.Open(), editor.ConfigFile());
	auto* explorer = window->findChild<ContentExplorerWindow*>();
	REQUIRE(explorer != nullptr);

	QTimer::singleShot(0, [] {
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* message = qobject_cast<QMessageBox*>(widget))
				message->accept();
	});
	CHECK_NOTHROW(Q_EMIT explorer->AssetOpenRequested("Authored/failure.bfixture"));
	CHECK(window->findChild<QWidget*>("sample.throwing_editor_child") == nullptr);
}
#endif

TEST_CASE(
	"Replacing a project replaces its built-in panels and render services",
	"[mainwindow][render]")
{
	const HeadlessEditor             first;
	const HeadlessEditor             second;
	MainWindow                       window(first.Plugins(), first.Open(), first.ConfigFile());
	QPointer<MaterialEditorWindow>   material  = window.findChild<MaterialEditorWindow*>();
	QPointer<AnimationEditorWindow>  animation = window.findChild<AnimationEditorWindow*>();
	QPointer<BlendSpaceEditorWindow> blend     = window.findChild<BlendSpaceEditorWindow*>();
	REQUIRE(material != nullptr);
	REQUIRE(animation != nullptr);
	REQUIRE(blend != nullptr);
	auto* scale = ActionNamed(window, "0.5x");
	REQUIRE(scale != nullptr);
	scale->trigger();
	auto* open = ActionNamed(window, "Open Project...");
	REQUIRE(open != nullptr);

	const auto sceneSlots = [&] {
		std::array<uint32_t, 2> allocations{};
		window.findChild<RenderTargetWindow*>()->Invoke(
			[&](editor::RenderContext& context, const bgl::SceneViewRef&) {
				const auto probeMaterial = context.scene.CreatePbrMaterial({});
				const auto probeGeom = context.scene.AddPlaneGeom(1, 1, 1.0f, 1.0f, probeMaterial);
				allocations          = { probeGeom.handle.index, probeMaterial.byteOffset };
				context.scene.DeleteGeom(probeGeom);
				context.scene.DeleteMaterial(probeMaterial);
			});
		return allocations;
	};
	const auto baseline = sceneSlots();
	for (int replacementIndex = 0; replacementIndex < 3; ++replacementIndex)
	{
		std::vector<fs::path> releasedRoots;
		QObject               teardownObserver;
		ObserveViewportTeardown(window, teardownObserver, releasedRoots);
		material                  = window.findChild<MaterialEditorWindow*>();
		animation                 = window.findChild<AnimationEditorWindow*>();
		blend                     = window.findChild<BlendSpaceEditorWindow*>();
		const bool nativeDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
		QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
		QElapsedTimer deadline;
		deadline.start();
		QTimer      chooser;
		bool        selected = false;
		QString     dialogMessage;
		QStringList selection;
		QObject::connect(&chooser, &QTimer::timeout, &window, [&] {
			for (QWidget* widget : QApplication::topLevelWidgets())
			{
				if (auto* message = qobject_cast<QMessageBox*>(widget))
				{
					dialogMessage = message->text();
					message->reject();
				}
				if (deadline.elapsed() > 10000)
				{
					if (auto* dialog = qobject_cast<QDialog*>(widget))
						dialog->reject();
					continue;
				}
				if (auto* dialog = qobject_cast<QFileDialog*>(widget);
				    dialog != nullptr && !selected)
				{
					auto* fileName = dialog->findChild<QLineEdit*>("fileNameEdit");
					if (fileName == nullptr)
						continue;
					fileName->setText(QString::fromStdString(second.ProjectFile().string()));
					selection = dialog->selectedFiles();
					selected  = true;
					static_cast<QDialog*>(dialog)->accept();
				}
			}
		});
		chooser.start(10);
		open->trigger();
		chooser.stop();
		QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled);
		INFO(dialogMessage.toStdString());
		INFO(selection.join(";").toStdString());
		REQUIRE(selected);
		CHECK(material.isNull());
		CHECK(animation.isNull());
		CHECK(blend.isNull());
		REQUIRE(releasedRoots.size() == c_ViewportCount);
		for (const auto& root : releasedRoots)
			CHECK(root == (replacementIndex == 0 ? first.DataRoot() : second.DataRoot()));
		auto* replacement = window.findChild<MaterialEditorWindow*>();
		REQUIRE(replacement != nullptr);
		CHECK(replacement->GetDataRoot() == second.DataRoot());
		const auto views = window.findChildren<RenderTargetWindow*>();
		REQUIRE(views.size() == c_ViewportCount);
		for (auto* view : views)
		{
			CHECK(view->GetRenderScale() == Catch::Approx(0.5f));
			fs::path root;
			view->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef&) {
				root = context.assets.GetStore().GetDataRoot();
			});
			CHECK(root == second.DataRoot());
		}
		CHECK(sceneSlots() == baseline);
	}
}

TEST_CASE("Tearing the editor down releases its viewports first", "[mainwindow][render]")
{
	const HeadlessEditor editor;

	std::vector<QPointer<RenderTargetWindow>> viewports;
	std::vector<fs::path>                     releasedRoots;
	QObject                                   teardownObserver;

	{
		auto window =
			std::make_unique<MainWindow>(editor.Plugins(), editor.Open(), editor.ConfigFile());
		ObserveViewportTeardown(*window, teardownObserver, releasedRoots);

		for (RenderTargetWindow* view : window->findChildren<RenderTargetWindow*>())
			viewports.emplace_back(view);

		REQUIRE(static_cast<int>(viewports.size()) == c_ViewportCount);
	}

	// ~RenderTargetWindow calls RemoveViewport and Invoke on the Renderer, so a viewport left for
	// ~QWidget to delete as a child reaches it after ~m_Renderer has run and dereferences a dead
	// one. ReleaseRenderResources deletes the panels ahead of that; a panel added later and not
	// deleted there segfaults this case, which is what the crash handler in main.cpp reports.
	//
	// The QPointers are the part that can fail cleanly: a viewport that survives its window without
	// touching the Renderer leaves one non-null rather than crashing.
	for (const QPointer<RenderTargetWindow>& view : viewports) CHECK(view.isNull());
	REQUIRE(releasedRoots.size() == c_ViewportCount);
	for (const auto& root : releasedRoots) CHECK(root == editor.DataRoot());
}

TEST_CASE("Opening a project roots every panel that follows it", "[mainwindow][render]")
{
	const HeadlessEditor editor;

	const MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());

	// The project is open by the time the constructor returns.
	auto* materials = window.findChild<MaterialEditorWindow*>();
	auto* animation = window.findChild<AnimationEditorWindow*>();

	REQUIRE(materials != nullptr);
	REQUIRE(animation != nullptr);

	CHECK(materials->GetDataRoot() == editor.DataRoot());
	CHECK(animation->GetDataRoot().toStdString() == editor.DataRoot().string());
}

// The project decides which surfaces the renderer compiles, which is why it is chosen first.
TEST_CASE(
	"The renderer registers the surfaces of the project the editor is built for",
	"[mainwindow][surfacerelaunch][render]")
{
	const HeadlessEditor editor;

	const fs::path other = editor.temp.path().toStdString() / fs::path("Other") /
	                       ("Other" + std::string(assetlib::Project::c_FileExtension));
	assetlib::Project::Create(other, "Other");
	core::file::write_atomic(
		assetlib::Project::DataDirectoryOf(other) / assetlib::c_ShadersDirectoryName / "Tint.slang",
		R"(import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;

struct TintParams
{
    float4 tint;
};

struct TintSurface : ISurfaceSource
{
    typealias MaterialParams = TintParams;

    static float Coverage<R : IMaterialReader>(R reader, TintParams params) { return params.tint.a; }

    static PbrSurface Evaluate<R : IMaterialReader>(R reader, TintParams params)
    {
        PbrSurface surface = PbrSurface();
        surface.baseColor = params.tint;
        return surface;
    }
};
)");

	const MainWindow window(editor.Plugins(), assetlib::Project::Open(other), editor.ConfigFile());

	auto* materials = window.findChild<MaterialEditorWindow*>();
	REQUIRE(materials != nullptr);

	CHECK(materials->GetDataRoot() == assetlib::Project::DataDirectoryOf(other));

	const QList<QComboBox*> combos = materials->findChildren<QComboBox*>();
	CHECK(std::ranges::any_of(combos, [](const QComboBox* combo) {
		return combo->findText(QStringLiteral("Tint")) >= 0;
	}));
}

// The bug this closes: the panels cleared off QDockWidget::visibilityChanged, and Qt reports every
// dock invisible when the window minimizes as well as when a tab is deselected -- so minimizing the
// editor threw away the mesh, its graphs and any unsaved edit to them.
TEST_CASE(
	"A minimized editor keeps the panel's mesh; leaving the tab still drops it",
	"[mainwindow][panelclear][render]")
{
	const HeadlessEditor editor;

	MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	window.show();

	auto* materialDock  = window.findChild<QDockWidget*>("bernini.material");
	auto* animationDock = window.findChild<QDockWidget*>("bernini.animation");
	auto* materials     = window.findChild<MaterialEditorWindow*>();
	auto* preview       = window.findChild<MaterialPreviewWindow*>();

	REQUIRE(materialDock != nullptr);
	REQUIRE(animationDock != nullptr);
	REQUIRE(materials != nullptr);
	REQUIRE(preview != nullptr);

	// The Material tab on top, as it is when a user switches away from the editor.
	materialDock->raise();
	REQUIRE(editor::test::WaitFor([materialDock] { return materialDock->isVisible(); }));

	// An external mesh exercises the plain-file preview without retargeting the project host.
	const fs::path dataRoot = fs::absolute("assets/Data");
	const fs::path mesh     = dataRoot / "Derived" / "Meshes" / "apples.bmesh";
	REQUIRE(fs::exists(mesh));

	preview->LoadMesh(mesh);
	REQUIRE_FALSE(preview->MeshPath().empty());

	window.showMinimized();
	QCoreApplication::processEvents();
	CHECK_FALSE(preview->MeshPath().empty());

	window.showNormal();
	QCoreApplication::processEvents();
	CHECK_FALSE(preview->MeshPath().empty());

	// Hidden as well as minimized: the window going away is one case however the platform spells it.
	window.hide();
	QCoreApplication::processEvents();
	CHECK_FALSE(preview->MeshPath().empty());

	window.show();
	REQUIRE(editor::test::WaitFor([&window] { return window.isVisible(); }));

	// And the half that must not change: leaving the tab still puts the default sphere back.
	animationDock->raise();
	CHECK(editor::test::WaitFor([preview] { return preview->MeshPath().empty(); }));
}

TEST_CASE("Every viewport a headless editor builds is headless", "[mainwindow][render]")
{
	const HeadlessEditor editor;

	const MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());

	const QList<RenderTargetWindow*> viewports = window.findChildren<RenderTargetWindow*>();

	// The Material Editor's preview, the Animation Editor's and the Blend Space Editor's. A panel added later fails this
	// line, which is the point: it then has to say whether it threads `headless` through, rather
	// than being window-backed in a suite that cannot realise a window.
	CHECK(static_cast<int>(viewports.size()) == c_ViewportCount);

	for (const RenderTargetWindow* view : viewports) CHECK(view->IsHeadless());
}

// How a viewport blooms is config.json's alone: the Render menu may switch it on and off and nothing
// else. A partial section overrides only what it names, and a value bgl would throw on is clamped
// rather than taking the editor down with it.
TEST_CASE(
	"A viewport blooms as config.json says, and the menu only toggles it",
	"[mainwindow][render]")
{
	const HeadlessEditor editor;

	const std::string config = R"({
  "headless": true,
  "materialEditor":  { "temporalAA": false,
                       "bloom": { "enabled": true, "intensity": 0.3, "threshold": 0.8,
                                  "softKnee": 5.0 } },
  "animationEditor": { "temporalAA": false }
})";
	core::file::write_atomic(editor.ConfigFile(), config);

	const MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());

	const auto* material = window.findChild<MaterialEditorWindow*>();
	REQUIRE(material != nullptr);
	const auto* materialView = material->findChild<RenderTargetWindow*>();
	REQUIRE(materialView != nullptr);

	CHECK(materialView->IsBloomEnabled());

	const bgl::BloomSettings named = materialView->GetBloomSettings();
	CHECK(named.intensity == Catch::Approx(0.3f));
	CHECK(named.threshold == Catch::Approx(0.8f));
	CHECK(named.softKnee == Catch::Approx(1.0f));
	CHECK(named.scatter == Catch::Approx(bgl::BloomSettings().scatter));

	// No section is bloom's default: off, and with the settings bgl ships.
	const auto* animation = window.findChild<AnimationEditorWindow*>();
	REQUIRE(animation != nullptr);
	const auto* animationView = animation->findChild<RenderTargetWindow*>();
	REQUIRE(animationView != nullptr);

	CHECK_FALSE(animationView->IsBloomEnabled());
	CHECK(
		animationView->GetBloomSettings().intensity ==
		Catch::Approx(bgl::BloomSettings().intensity));

	// Checked because a viewport started with it on; and a toggle is all the menu offers.
	const QAction* bloom = ActionNamed(window, "Bloom");
	REQUIRE(bloom != nullptr);
	CHECK(bloom->isChecked());

	for (const char* valueMenu :
	     { "Bloom Intensity", "Bloom Threshold", "Bloom Soft Knee", "Bloom Scatter" })
	{
		INFO(valueMenu);
		CHECK(ActionNamed(window, valueMenu) == nullptr);
	}
}

// As bloom: how a viewport grades is config.json's, and the Render menu only switches it. A partial
// CDL channel set overrides only the channels it names, and a value bgl would throw on is clamped.
TEST_CASE(
	"A viewport grades as config.json says, and the menu only toggles it",
	"[mainwindow][render]")
{
	const HeadlessEditor editor;

	const std::string config = R"({
  "headless": true,
  "materialEditor":  { "temporalAA": false,
                       "colorGrade": { "enabled": true, "temperature": 20.0, "saturation": 1.3,
                                       "slope": { "r": 1.1 }, "power": { "g": 0.0 },
                                       "tint": 250.0 } },
  "animationEditor": { "temporalAA": false }
})";
	core::file::write_atomic(editor.ConfigFile(), config);

	MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());

	const auto* material = window.findChild<MaterialEditorWindow*>();
	REQUIRE(material != nullptr);
	auto* materialView = material->findChild<RenderTargetWindow*>();
	REQUIRE(materialView != nullptr);

	CHECK(materialView->IsColorGradeEnabled());

	const bgl::ColorGradeSettings named = materialView->GetColorGradeSettings();
	CHECK(named.temperature == Catch::Approx(20.0f));
	CHECK(named.saturation == Catch::Approx(1.3f));
	CHECK(named.slope.r == Catch::Approx(1.1f));
	CHECK(named.slope.g == Catch::Approx(1.0f));
	CHECK(named.power.g > 0.0f);
	CHECK(named.tint == Catch::Approx(100.0f));
	CHECK(named.contrast == Catch::Approx(DefaultViewportGrade().contrast));

	// No section is the editor's default grade, off -- and not bgl's neutral one, or the Render
	// menu's toggle would switch between two identical images.
	const auto* animation = window.findChild<AnimationEditorWindow*>();
	REQUIRE(animation != nullptr);
	const auto* animationView = animation->findChild<RenderTargetWindow*>();
	REQUIRE(animationView != nullptr);

	CHECK_FALSE(animationView->IsColorGradeEnabled());

	const bgl::ColorGradeSettings unnamed = animationView->GetColorGradeSettings();
	CHECK(unnamed.saturation == Catch::Approx(DefaultViewportGrade().saturation));
	CHECK(unnamed.saturation != Catch::Approx(bgl::ColorGradeSettings().saturation));
	CHECK(unnamed.vignetteIntensity > 0.0f);

	QAction* grade = ActionNamed(window, "Color Grade");
	REQUIRE(grade != nullptr);
	CHECK(grade->isChecked());

	// Unchecking switches every viewport off and leaves what config.json named in place.
	grade->setChecked(false);
	CHECK_FALSE(materialView->IsColorGradeEnabled());
	CHECK(materialView->GetColorGradeSettings().saturation == Catch::Approx(1.3f));
}

// Which tab is up decides which viewport is in the frame loop, so the tab a project opens on is
// behaviour rather than layout: the panel behind it holds no mesh and renders nothing.
TEST_CASE("A project opens on the Material Editor tab", "[mainwindow][render]")
{
	const HeadlessEditor editor;

	MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	window.show();
	QCoreApplication::processEvents();

	// The dock group's own bar, which QMainWindow parents to itself -- not the Animation panel's
	// Clip/Blend one, which is a bar inside a dock.
	const QList<QTabBar*> bars   = window.findChildren<QTabBar*>();
	const auto            docked = std::ranges::find_if(bars, [&window](const QTabBar* bar) {
		return bar->parentWidget() == &window;
	});

	REQUIRE(docked != bars.end());
	CHECK((*docked)->tabText((*docked)->currentIndex()) == QStringLiteral("Material Editor"));
}

// The graph costs a resolve per frame to fill, so it turns timing on for itself rather than opening
// empty behind a menu item somebody was supposed to find first -- and gives back what it borrowed.
TEST_CASE("The timing graph turns GPU timing on while it is open", "[mainwindow][render]")
{
	const HeadlessEditor editor;

	MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());

	QAction* timing = ActionNamed(window, "GPU Pass Timing");
	QAction* graph  = ActionNamed(window, "GPU Timing Graph");
	REQUIRE(timing != nullptr);
	REQUIRE(graph != nullptr);
	REQUIRE_FALSE(timing->isChecked());

	auto* readout = window.findChild<editor::GpuTimingWindow*>();
	REQUIRE(readout != nullptr);

	graph->setChecked(true);
	CHECK(timing->isChecked());

	readout->close();
	CHECK_FALSE(timing->isChecked());

	// The entry is the window's own state, so closing it from its title bar unchecks the box: an
	// entry left checked beside a closed window makes the next click do nothing.
	CHECK_FALSE(graph->isChecked());

	// Switched on for the log before the window was opened, it stays on after it closes: the window
	// restores what it found rather than switching off something it did not turn on.
	timing->setChecked(true);
	graph->setChecked(true);
	readout->close();
	CHECK(timing->isChecked());
}

TEST_CASE("A panel is rooted without being listed anywhere", "[project]")
{
	auto  root   = QObject();
	auto* direct = new SpyPanel(&root);
	auto* nested = new SpyPanel(new QObject(&root));

	editor::SetProjectDataRoot(&root, "/projects/MyGame/Data");

	// Depth is not what decides it: a preview buried under a panel's splitter is as much a follower
	// as one parented straight to the window.
	CHECK(direct->root == "/projects/MyGame/Data");
	CHECK(nested->root == "/projects/MyGame/Data");

	// Told once, so a panel that rebuilds state on every call is not made to do it twice.
	CHECK(direct->calls == 1);
	CHECK(nested->calls == 1);
}

TEST_CASE("Rooting a tree with no followers in it does nothing", "[project]")
{
	auto root = QObject();
	new QObject(&root);

	CHECK_NOTHROW(editor::SetProjectDataRoot(&root, "/projects/MyGame/Data"));
	CHECK_NOTHROW(editor::SetProjectDataRoot(nullptr, "/projects/MyGame/Data"));
}

TEST_CASE("Building the editor reports what it is doing", "[mainwindow][startup][render]")
{
	const HeadlessEditor editor;

	// What main.cpp hands the window, and the only thing that can report a cold start: by the time
	// there is a window to say "compiling", the compiling is over.
	auto labels = std::vector<QString>();

	{
		const MainWindow window(
			editor.Plugins(),
			editor.Open(),
			editor.ConfigFile(),
			[&](int, int, const QString& label) { labels.push_back(label); });
	}

	// Landing nothing is the failure this pins, and it is invisible: the window builds exactly as
	// it does now and the screen sits on "Starting..." for the whole cold start.
	REQUIRE_FALSE(labels.empty());
	CHECK(labels.front() == QStringLiteral("Compiling shaders..."));

	// The shaders are one step because bgl builds every pipeline inside CreateGraphics; what
	// follows is the project, which reports per file.
	CHECK(std::ranges::count(labels, QStringLiteral("Compiling shaders...")) == 1);
}

static_assert(
	!std::is_constructible_v<MainWindow> && !std::is_constructible_v<MainWindow, QWidget*>,
	"there is no editor without a project: the landing page chooses one before the renderer "
	"exists");

TEST_CASE(
	"Opening a project records it among the recent ones",
	"[mainwindow][recentprojects][render]")
{
	const HeadlessEditor editor;

	{
		const MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	}

	// Beside the config the window read, which is how a test keeps off the shipping editor's list.
	const std::vector<fs::path> recent =
		editor::ReadRecentProjects(editor::RecentProjectsFileBeside(editor.ConfigFile()));
	REQUIRE_FALSE(recent.empty());
	CHECK(fs::equivalent(recent.front(), editor.ProjectFile()));
}

namespace
{
	/**
	 * A set of one space over a clip set whose rig nothing is skinned to: an authored document with
	 * nothing to show it on. Returns its key.
	 */
	QString
	WriteUnshownSet(const fs::path& dataRoot)
	{
		editor::test::WriteBanim(
			dataRoot,
			"Derived/Animations/loco.banim",
			"Derived/Skeletons/rig.bskel");

		auto set       = assetlib::BlendSet();
		set.animations = "Derived/Animations/loco.banim";
		set.spaces.push_back({ "Locomotion", { { "walk", 0.0f }, { "run", 1.0f } } });

		const std::string key = "Authored/Animations/loco.bblend";
		assetlib::AssetStore(dataRoot).Save(set, key);
		return QString::fromStdString(key);
	}
}

TEST_CASE(
	"The Blend Space Editor is a tab of the editors' group",
	"[mainwindow][blendspace][render]")
{
	const HeadlessEditor editor;

	const MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());

	auto* animationDock = window.findChild<QDockWidget*>("bernini.animation");
	auto* blendDock     = window.findChild<QDockWidget*>("bernini.blend_space");
	REQUIRE(animationDock != nullptr);
	REQUIRE(blendDock != nullptr);

	CHECK(window.tabifiedDockWidgets(animationDock).contains(blendDock));

	// Neither movable nor floatable: a tab dragged out would put a second viewport into the frame loop.
	CHECK(blendDock->features() == QDockWidget::DockWidgetClosable);
}

TEST_CASE(
	"A blend space is not authored on the Animation panel",
	"[mainwindow][blendspace][render]")
{
	const HeadlessEditor editor;

	const MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());

	auto* animation = window.findChild<AnimationEditorWindow*>();
	REQUIRE(animation != nullptr);

	auto* surfaces = animation->findChild<QTabWidget*>();
	REQUIRE(surfaces != nullptr);

	auto tabs = QStringList();
	for (int i = 0; i < surfaces->count(); ++i) tabs << surfaces->tabText(i);

	CHECK(tabs == QStringList({ QStringLiteral("Clip"), QStringLiteral("Blend") }));
}

TEST_CASE(
	"Animation properties fit beside a visible scrollbar",
	"[mainwindow][animation][layout][render]")
{
	const HeadlessEditor editor;
	MainWindow           window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	window.resize(1200, 700);
	window.show();
	auto* dock = window.findChild<QDockWidget*>("bernini.animation");
	REQUIRE(dock != nullptr);
	dock->show();
	dock->raise();
	auto* animation = window.findChild<AnimationEditorWindow*>();
	REQUIRE(animation != nullptr);
	auto* scroll   = animation->findChild<QScrollArea*>();
	auto* splitter = animation->findChild<QSplitter*>();
	REQUIRE(scroll != nullptr);
	REQUIRE(splitter != nullptr);
	scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	splitter->setSizes({ scroll->minimumWidth(), 1000 });
	QCoreApplication::processEvents();
	REQUIRE(scroll->verticalScrollBar()->isVisible());
	CHECK(scroll->widget()->width() <= scroll->viewport()->width());
	CHECK(scroll->horizontalScrollBar()->maximum() == 0);
}

TEST_CASE(
	"A blend set with nothing to show it on still opens, and still edits",
	"[mainwindow][blendspace][render]")
{
	const HeadlessEditor editor;
	const QString        key = WriteUnshownSet(editor.DataRoot());

	const MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());

	auto* blend = window.findChild<BlendSpaceEditorWindow*>();
	REQUIRE(blend != nullptr);

	blend->OpenBlendSet(key);
	REQUIRE(blend->GetBlendSetKey() == key);

	auto* samples  = blend->findChild<QListWidget*>("BlendSpaceSamples");
	auto* addSpace = blend->findChild<QPushButton*>("AddBlendSpace");
	auto* remove   = blend->findChild<QPushButton*>("RemoveBlendSpace");
	auto* note     = blend->findChild<QLabel*>("BlendSpaceViewNote");
	REQUIRE(samples != nullptr);
	REQUIRE(addSpace != nullptr);
	REQUIRE(remove != nullptr);
	REQUIRE(note != nullptr);

	// Listed from the document, which needs no rig.
	CHECK(samples->count() == 2);
	CHECK(note->text().contains(QStringLiteral("Nothing is skinned")));

	const QStringList held = blend->GetHeldOpenPaths();
	CHECK(std::ranges::any_of(held, [&key](const QString& path) { return path.endsWith(key); }));

	// Seeding a space takes clips, and only an acquired rig lists them.
	CHECK_FALSE(addSpace->isEnabled());

	REQUIRE(remove->isEnabled());
	remove->click();

	const auto saved =
		assetlib::AssetStore(editor.DataRoot()).Load<assetlib::BlendSet>(key.toStdString());
	CHECK(saved.spaces.empty());
	CHECK(samples->count() == 0);
}

TEST_CASE(
	"A threshold typed past its neighbour re-sorts the run, and is saved that way",
	"[mainwindow][blendspace][render]")
{
	const HeadlessEditor editor;
	const QString        key = WriteUnshownSet(editor.DataRoot());

	const MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());

	auto* blend = window.findChild<BlendSpaceEditorWindow*>();
	REQUIRE(blend != nullptr);
	blend->OpenBlendSet(key);

	auto* samples   = blend->findChild<QListWidget*>("BlendSpaceSamples");
	auto* threshold = blend->findChild<QDoubleSpinBox*>("BlendSampleThreshold");
	REQUIRE(samples != nullptr);
	REQUIRE(threshold != nullptr);
	REQUIRE(samples->count() == 2);

	samples->setCurrentRow(0);
	REQUIRE(threshold->isEnabled());

	SECTION("walk typed past run becomes the second row, and stays selected")
	{
		threshold->setValue(2.0);

		CHECK(samples->currentRow() == 1);
		CHECK(samples->item(0)->text().startsWith(QStringLiteral("run")));
		CHECK(samples->item(1)->text().startsWith(QStringLiteral("walk")));
		CHECK(threshold->value() == Catch::Approx(2.0));

		const auto saved =
			assetlib::AssetStore(editor.DataRoot()).Load<assetlib::BlendSet>(key.toStdString());
		REQUIRE(saved.spaces.size() == 1);
		REQUIRE(saved.spaces[0].samples.size() == 2);
		CHECK(saved.spaces[0].samples[0].clip == "run");
		CHECK(saved.spaces[0].samples[1].clip == "walk");
	}

	SECTION("typed onto run's threshold, the box goes back and nothing moves")
	{
		threshold->setValue(1.0);

		CHECK(samples->currentRow() == 0);
		CHECK(threshold->value() == Catch::Approx(0.0));
		CHECK(samples->item(0)->text().startsWith(QStringLiteral("walk")));
	}
}

TEST_CASE("Leaving the Blend Space Editor's tab closes the set", "[mainwindow][blendspace][render]")
{
	const HeadlessEditor editor;
	const QString        key = WriteUnshownSet(editor.DataRoot());

	MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	window.show();

	auto* animationDock = window.findChild<QDockWidget*>("bernini.animation");
	auto* blendDock     = window.findChild<QDockWidget*>("bernini.blend_space");
	auto* blend         = window.findChild<BlendSpaceEditorWindow*>();
	REQUIRE(animationDock != nullptr);
	REQUIRE(blendDock != nullptr);
	REQUIRE(blend != nullptr);

	blendDock->raise();
	REQUIRE(editor::test::WaitFor([blendDock] { return blendDock->isVisible(); }));

	blend->OpenBlendSet(key);
	REQUIRE(blend->GetBlendSetKey() == key);

	animationDock->raise();
	CHECK(editor::test::WaitFor([blend] { return blend->GetBlendSetKey().isEmpty(); }));
	CHECK(blend->GetHeldOpenPaths().isEmpty());
}

TEST_CASE(
	"A blend set is started from the Blend Space Editor, not the Animation panel",
	"[mainwindow][blendspace][render]")
{
	const HeadlessEditor editor;

	const MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());

	auto* animation = window.findChild<AnimationEditorWindow*>();
	auto* blend     = window.findChild<BlendSpaceEditorWindow*>();
	REQUIRE(animation != nullptr);
	REQUIRE(blend != nullptr);

	const QList<QPushButton*> animationButtons = animation->findChildren<QPushButton*>();
	CHECK(std::ranges::none_of(animationButtons, [](const QPushButton* button) {
		return button->text().contains(QStringLiteral("Blend Set"));
	}));

	auto* start = blend->findChild<QPushButton*>("NewBlendSet");
	REQUIRE(start != nullptr);
	CHECK(start->isEnabled());
}

TEST_CASE(
	"A blend set double-clicked in the Content Explorer opens in the Blend Space Editor",
	"[mainwindow][blendspace][render]")
{
	const HeadlessEditor editor;
	const QString        key = WriteUnshownSet(editor.DataRoot());

	MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	window.show();

	auto* blendDock = window.findChild<QDockWidget*>("bernini.blend_space");
	auto* blend     = window.findChild<BlendSpaceEditorWindow*>();
	auto* explorer  = window.findChild<ContentExplorerWindow*>();
	REQUIRE(blendDock != nullptr);
	REQUIRE(blend != nullptr);
	REQUIRE(explorer != nullptr);

	auto* files = explorer->findChild<QListView*>("CurrentDirectoryExplorer");
	REQUIRE(files != nullptr);
	auto* model = qobject_cast<QFileSystemModel*>(files->model());
	REQUIRE(model != nullptr);

	const QString path = QDir::fromNativeSeparators(
		QDir(QString::fromStdString(editor.DataRoot().string())).absoluteFilePath(key));

	QModelIndex tile;
	REQUIRE(editor::test::WaitFor([&] {
		tile = model->index(path);
		return tile.isValid();
	}));

	// The editors' own tab bar, which QMainWindow parents to itself: a tabified dock is not hidden
	// when another tab is current, so its visibility cannot say which one is on top.
	const QList<QTabBar*> bars   = window.findChildren<QTabBar*>();
	const auto            docked = std::ranges::find_if(bars, [&window](const QTabBar* bar) {
		return bar->parentWidget() == &window;
	});
	REQUIRE(docked != bars.end());

	const auto current  = [bar = *docked] { return bar->tabText(bar->currentIndex()); };
	const auto blendTab = QStringLiteral("Blend Space Editor");

	// A project opens on the Material Editor's tab, so the double-click is what brings this one up.
	QCoreApplication::processEvents();
	REQUIRE(current() != blendTab);

	Q_EMIT files->doubleClicked(tile);

	CHECK(editor::test::WaitFor([&] { return current() == blendTab; }));
	CHECK(blendDock->isVisible());
	CHECK(blend->GetBlendSetKey() == key);

	// Still open once the tab switch has settled: raising the dock must not be what closes the set.
	QCoreApplication::processEvents();
	CHECK(blend->GetBlendSetKey() == key);
}

TEST_CASE(
	"Both rig previews carry the Plant feet group, off and collapsed",
	"[mainwindow][blendspace][render]")
{
	const HeadlessEditor editor;

	const MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());

	auto* animation = window.findChild<AnimationEditorWindow*>();
	auto* blend     = window.findChild<BlendSpaceEditorWindow*>();
	REQUIRE(animation != nullptr);
	REQUIRE(blend != nullptr);

	for (QWidget* panel : { static_cast<QWidget*>(animation), static_cast<QWidget*>(blend) })
	{
		INFO("panel: " << panel->metaObject()->className());

		auto* ground = panel->findChild<GroundControls*>();
		REQUIRE(ground != nullptr);

		const QList<Scrubber*> sliders = ground->findChildren<Scrubber*>();
		CHECK(sliders.size() == 4);

		// Off, so a panel just opened shows the clip as authored, and the sliders fold away with it.
		CHECK_FALSE(ground->isChecked());
		CHECK(std::ranges::none_of(sliders, [ground](const Scrubber* slider) {
			return slider->isVisibleTo(ground);
		}));

		ground->setChecked(true);
		CHECK(std::ranges::all_of(sliders, [ground](const Scrubber* slider) {
			return slider->isVisibleTo(ground);
		}));
	}
}

TEST_CASE(
	"Blend documents open through the registered asset editor and reuse its startup tab",
	"[mainwindow][render][rigplugin]")
{
	const HeadlessEditor       editor;
	const assetlib::AssetStore store(editor.DataRoot());
	auto                       set = assetlib::BlendSet();
	set.animations                 = "Derived/Animations/absent.banim";
	const std::string key          = "Authored/Animations/solo.bblend";
	store.Save(set, key);
	MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	window.show();
	QCoreApplication::processEvents();
	auto* dock = window.findChild<QDockWidget*>("bernini.blend_space");
	REQUIRE(dock != nullptr);
	auto* panel = dynamic_cast<editor::AssetEditorPanel*>(dock->widget());
	REQUIRE(panel != nullptr);
	auto* explorer = window.findChild<ContentExplorerWindow*>();
	REQUIRE(explorer != nullptr);
	for (int request = 0; request < 2; ++request)
	{
		Q_EMIT explorer->AssetOpenRequested(QString::fromStdString(key));
		QCoreApplication::processEvents();
		CHECK(window.findChildren<QDockWidget*>("bernini.blend_space").size() == 1);
		CHECK(dock->widget() == panel);
		CHECK(panel->GetHeldAssets() == std::vector<std::string>{ key, set.animations });
	}
	window.findChild<QDockWidget*>("bernini.material")->raise();
	QCoreApplication::processEvents();
	CHECK(panel->GetHeldAssets().empty());
}

TEST_CASE(
	"Blend plugin close commits a pending threshold and vetoes a failed save",
	"[mainwindow][render][rigplugin]")
{
	const HeadlessEditor       editor;
	const assetlib::AssetStore store(editor.DataRoot());
	auto                       set = assetlib::BlendSet();
	set.animations                 = "Derived/Animations/absent.banim";
	set.spaces                     = { { "Speed", { { "Walk", 0.0f }, { "Run", 1.0f } } } };
	const std::string key          = "Authored/Animations/pending.bblend";
	store.Save(set, key);
	MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	auto*      dock = window.findChild<QDockWidget*>("bernini.blend_space");
	REQUIRE(dock != nullptr);
	auto* panel = dynamic_cast<editor::AssetEditorPanel*>(dock->widget());
	REQUIRE(panel != nullptr);
	panel->OpenAsset(key);
	auto* samples   = panel->findChild<QListWidget*>("BlendSpaceSamples");
	auto* threshold = panel->findChild<QDoubleSpinBox*>("BlendSampleThreshold");
	REQUIRE(samples != nullptr);
	REQUIRE(threshold != nullptr);
	samples->setCurrentRow(0);
	REQUIRE(threshold->isEnabled());
	threshold->setValue(0.25);
	CHECK(store.Load<assetlib::BlendSet>(key).spaces.front().samples.front().parameter == 0.0f);
	const auto path = store.ResolveWritePath(key);
	REQUIRE(fs::remove(path));
	REQUIRE(fs::create_directory(path));
	QTimer dismiss;
	QObject::connect(&dismiss, &QTimer::timeout, &window, [] {
		for (auto* widget : QApplication::topLevelWidgets())
			if (auto* message = qobject_cast<QMessageBox*>(widget))
				message->accept();
	});
	dismiss.start(10);
	CHECK_FALSE(panel->CanClose());
	dismiss.stop();
	CHECK(threshold->value() == 0.25);
	REQUIRE(fs::remove(path));
	CHECK(panel->CanClose());
	CHECK(store.Load<assetlib::BlendSet>(key).spaces.front().samples.front().parameter == 0.25f);
}

TEST_CASE(
	"Rig plugin viewports accept environment drops and restore the project configuration",
	"[mainwindow][render][rigplugin]")
{
	const HeadlessEditor editor;
	const auto           configured = editor.DataRoot() / "Authored/Environments/configured.benv";
	std::ofstream(editor.ConfigFile()) << nlohmann::json{
		{ "headless", true },
		{ "startupProject", editor.ProjectFile().generic_string() },
		{ "materialEditor", { { "temporalAA", false } } },
		{ "animationEditor",
		  { { "temporalAA", false }, { "environmentMap", configured.generic_string() } } }
	}.dump(2);
	MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	for (const auto* id : { "bernini.animation", "bernini.blend_space" })
	{
		auto* dock = window.findChild<QDockWidget*>(id);
		REQUIRE(dock != nullptr);
		auto* panel = dynamic_cast<editor::EditorPanel*>(dock->widget());
		REQUIRE(panel != nullptr);
		CHECK(
			panel->GetHeldAssets() ==
			std::vector<std::string>{ "Authored/Environments/configured.benv" });
		auto* view = panel->findChild<RenderTargetWindow*>();
		REQUIRE(view != nullptr);
		QMimeData mime;
		mime.setUrls(
			{ QUrl::fromLocalFile(
				QString::fromStdString(
					(editor.DataRoot() / "Authored/Environments/dropped.benv").string())) });
		QDragEnterEvent enter(QPoint(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(view, &enter);
		REQUIRE(enter.isAccepted());
		QDropEvent drop(QPointF(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(view, &drop);
		REQUIRE(drop.isAccepted());
		CHECK(
			panel->GetHeldAssets() ==
			std::vector<std::string>{ "Authored/Environments/dropped.benv" });
		panel->SetActive(false);
		CHECK(
			panel->GetHeldAssets() ==
			std::vector<std::string>{ "Authored/Environments/configured.benv" });
	}
}

TEST_CASE(
	"Material plugin viewport receives native-widget picking input",
	"[mainwindow][render][materialplugin]")
{
	const HeadlessEditor editor;
	MainWindow           window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	window.show();
	QCoreApplication::processEvents();
	auto* preview = window.findChild<MaterialPreviewWindow*>();
	REQUIRE(preview != nullptr);
	auto* view = preview->findChild<RenderTargetWindow*>();
	REQUIRE(view != nullptr);
	CHECK(view->parentWidget() == preview);
	QSignalSpy picked(preview, &MaterialPreviewWindow::SubmeshPicked);
	QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, view->rect().center());
	REQUIRE(picked.count() == 1);
	CHECK(picked.front().front().toInt() == 0);
}

TEST_CASE(
	"Material retains its configured project environment without an explicit data root",
	"[mainwindow][render][materialplugin]")
{
	const HeadlessEditor editor;
	const auto           configured = editor.DataRoot() / "Authored/Environments/pending.benv";
	std::ofstream(editor.ConfigFile()) << nlohmann::json{
		{ "headless", true },
		{ "startupProject", editor.ProjectFile().generic_string() },
		{ "materialEditor",
		  { { "temporalAA", false }, { "environmentMap", configured.generic_string() } } },
		{ "animationEditor", { { "temporalAA", false }, { "environmentMap", "" } } }
	}.dump(2);
	MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	auto*      panel = window.findChild<MaterialEditorWindow*>();
	REQUIRE(panel != nullptr);
	auto* preview = panel->findChild<MaterialPreviewWindow*>();
	REQUIRE(preview != nullptr);
	const std::vector<std::string> expected{ "Authored/Environments/pending.benv" };
	// An unavailable environment remains held while the user repairs its files.
	CHECK(panel->GetHeldAssets() == expected);
	QMimeData mime;
	mime.setUrls(
		{ QUrl::fromLocalFile(
			QString::fromStdString(
				(editor.DataRoot() / "Authored/Environments/dropped.benv").string())) });
	auto* view = preview->findChild<RenderTargetWindow*>();
	REQUIRE(view != nullptr);
	QDragEnterEvent enter(QPoint(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(view, &enter);
	REQUIRE(enter.isAccepted());
	QDropEvent drop(QPointF(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(view, &drop);
	REQUIRE(drop.isAccepted());
	CHECK(
		panel->GetHeldAssets() == std::vector<std::string>{ "Authored/Environments/dropped.benv" });
	panel->Reset();
	CHECK(panel->GetHeldAssets() == expected);
}

TEST_CASE(
	"An explorer bake refreshes the registered Material panel",
	"[mainwindow][render][materialplugin]")
{
	const HeadlessEditor       editor;
	const assetlib::AssetStore store(editor.DataRoot());
	auto                       material = assetlib::BMaterial();
	material.name                       = "Notification";
	const std::string key               = "Authored/Materials/notification.bmaterial";

	// A routed source that exists, against a stamp that was never taken: the material reads as
	// baked-stale, which is what the panel puts on its Bake All button.
	const std::string source = "Derived/SourceTextures/before.ktx2";
	fs::create_directories(store.ResolveWritePath(source).parent_path());
	std::ofstream(store.ResolveWritePath(source)) << "not really a texture";
	material.pbr.routes[0].texture = source;
	store.Save(material, key);
	MainWindow window(editor.Plugins(), editor.Open(), editor.ConfigFile());
	auto*      panel = window.findChild<MaterialEditorWindow*>();
	REQUIRE(panel != nullptr);
	REQUIRE(dynamic_cast<editor::EditorPanel*>(panel) != nullptr);
	QPushButton* open = nullptr;
	for (auto* button : panel->findChildren<QPushButton*>())
		if (button->text() == "Open...")
			open = button;
	REQUIRE(open != nullptr);
	REQUIRE(open->isEnabled());
	const bool nativeDisabled = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
	QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	QTimer        chooser;
	QElapsedTimer deadline;
	deadline.start();
	bool selected = false;
	QObject::connect(&chooser, &QTimer::timeout, &window, [&] {
		for (auto* widget : QApplication::topLevelWidgets())
		{
			if (auto* message = qobject_cast<QMessageBox*>(widget))
				message->reject();
			if (auto* dialog = qobject_cast<QFileDialog*>(widget))
			{
				if (deadline.elapsed() > 10000)
				{
					dialog->reject();
					continue;
				}
				if (auto* name = dialog->findChild<QLineEdit*>("fileNameEdit"))
				{
					name->setText(QString::fromStdString(store.ResolveWritePath(key).string()));
					selected = true;
					static_cast<QDialog*>(dialog)->accept();
				}
			}
		}
	});
	chooser.start(10);
	open->click();
	chooser.stop();
	QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled);
	REQUIRE(selected);
	CHECK(panel->GetHeldAssets() == std::vector<std::string>{ key });

	// The panel says nothing about paths any more: what a bake elsewhere changes here is the badge
	// on the button that would have fixed it.
	const auto marked = [panel]() {
		for (auto* button : panel->findChildren<QPushButton*>())
			if (button->text().startsWith("Bake All"))
				return button->text() != QStringLiteral("Bake All");
		return false;
	};
	REQUIRE(editor::test::WaitFor(marked));

	// What a bake leaves behind, without one: nothing routed, so nothing to have drifted. The
	// file's timestamp is put back, so only the notification can be what the panel acted on.
	const auto file  = store.ResolveWritePath(key);
	const auto stamp = fs::last_write_time(file);
	material.pbr.routes[0].texture.clear();
	store.Save(material, key);
	fs::last_write_time(file, stamp);
	CHECK(editor::test::WaitFor(marked));

	auto* explorer = window.findChild<ContentExplorerWindow*>();
	REQUIRE(explorer != nullptr);
	Q_EMIT explorer->MaterialBaked(QString::fromStdString(key));
	QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
	CHECK(editor::test::WaitFor([&marked] { return !marked(); }));
	CHECK(panel->GetHeldAssets() == std::vector<std::string>{ key });
}
