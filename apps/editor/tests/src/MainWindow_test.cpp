#include "MainWindow.h"

#include "Windows/AnimationEditor/AnimationEditorWindow.h"
#include "Windows/BlendSpaceEditor/BlendSpaceEditorWindow.h"
#include "Windows/GpuTiming/GpuTimingWindow.h"
#include "Windows/MaterialEditor/MaterialEditorWindow.h"
#include "Windows/MaterialEditor/MaterialPreviewWindow.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "util/QtSupport.h"  // IWYU pragma: keep
#include "util/follows_project.h"
#include "util/rig_containers.h"
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/blend.h>

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <core/file/file.h>
#include <filesystem>
#include <memory>
#include <qlist.h>
#include <qmainwindow.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <string>
#include <vector>

// What a viewport's `headless` flag buys: a whole editor -- device, renderer, every viewport --
// standing in a test, so its construction and teardown are pinned rather than found by driving the
// app. What it draws is still bgl_extended_tests' job.

namespace
{
	namespace fs = std::filesystem;

	// The panels that own a viewport today. The count below is what makes another one loud.
	constexpr int c_ViewportCount = 3;

	/** A scaffolded project and a config.json naming it, both in a directory of their own. */
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
  "startupProject": ")" + EscapedProjectFile() +
			                           R"(",
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

		// JSON has no raw backslash, and a Windows path is full of them.
		[[nodiscard]] std::string
		EscapedProjectFile() const
		{
			std::string escaped;
			for (const char c : ProjectFile().string())
			{
				if (c == '\\')
					escaped += '\\';
				escaped += c;
			}
			return escaped;
		}
	};

	[[nodiscard]] QAction*
	ActionNamed(const MainWindow& window, const QString& text)
	{
		const QList<QAction*> actions = window.findChildren<QAction*>();
		const auto            named =
			std::ranges::find_if(actions, [&text](const QAction* a) { return a->text() == text; });

		return named == actions.end() ? nullptr : *named;
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

TEST_CASE("Tearing the editor down releases its viewports first", "[mainwindow][render]")
{
	const HeadlessEditor editor;

	std::vector<QPointer<RenderTargetWindow>> viewports;

	{
		auto window = std::make_unique<MainWindow>(nullptr, editor.ConfigFile());

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
}

TEST_CASE("Opening a project roots every panel that follows it", "[mainwindow][render]")
{
	const HeadlessEditor editor;

	const MainWindow window(nullptr, editor.ConfigFile());

	// config.json's startupProject is the only route into SetActiveProject that opens no dialog,
	// so the project is already open by the time the constructor returns.
	auto* materials = window.findChild<MaterialEditorWindow*>();
	auto* animation = window.findChild<AnimationEditorWindow*>();

	REQUIRE(materials != nullptr);
	REQUIRE(animation != nullptr);

	CHECK(materials->GetDataRoot() == editor.DataRoot());
	CHECK(animation->GetDataRoot().toStdString() == editor.DataRoot().string());
}

// The bug this closes: the panels cleared off QDockWidget::visibilityChanged, and Qt reports every
// dock invisible when the window minimizes as well as when a tab is deselected -- so minimizing the
// editor threw away the mesh, its graphs and any unsaved edit to them.
TEST_CASE(
	"A minimized editor keeps the panel's mesh; leaving the tab still drops it",
	"[mainwindow][panelclear][render]")
{
	const HeadlessEditor editor;

	MainWindow window(nullptr, editor.ConfigFile());
	window.show();

	auto* materialDock  = window.findChild<QDockWidget*>("MaterialEditorDock");
	auto* animationDock = window.findChild<QDockWidget*>("AnimationEditorDock");
	auto* materials     = window.findChild<MaterialEditorWindow*>();
	auto* preview       = window.findChild<MaterialPreviewWindow*>();

	REQUIRE(materialDock != nullptr);
	REQUIRE(animationDock != nullptr);
	REQUIRE(materials != nullptr);
	REQUIRE(preview != nullptr);

	// The Material tab on top, as it is when a user switches away from the editor.
	materialDock->raise();
	REQUIRE(editor::test::WaitFor([materialDock] { return materialDock->isVisible(); }));

	// apples.bmesh names its materials relative to the shared asset directory, so that is the root
	// the panel has to resolve them against -- not the scaffolded project's empty one.
	const fs::path dataRoot = fs::absolute("assets/Data");
	const fs::path mesh     = dataRoot / "Derived" / "Meshes" / "apples.bmesh";
	REQUIRE(fs::exists(mesh));

	materials->SetDataRoot(QString::fromStdString(dataRoot.string()));
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

	const MainWindow window(nullptr, editor.ConfigFile());

	const QList<RenderTargetWindow*> viewports = window.findChildren<RenderTargetWindow*>();

	// The Material Editor's preview, the Animation Editor's and the Blend Space Editor's. A panel added later fails this
	// line, which is the point: it then has to say whether it threads `headless` through, rather
	// than being window-backed in a suite that cannot realise a window.
	CHECK(static_cast<int>(viewports.size()) == c_ViewportCount);

	for (const RenderTargetWindow* view : viewports) CHECK(view->IsHeadless());
}

// Which tab is up decides which viewport is in the frame loop, so the tab a project opens on is
// behaviour rather than layout: the panel behind it holds no mesh and renders nothing.
TEST_CASE("A project opens on the Material Editor tab", "[mainwindow][render]")
{
	const HeadlessEditor editor;

	MainWindow window(nullptr, editor.ConfigFile());
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

	MainWindow window(nullptr, editor.ConfigFile());

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
		const MainWindow window(nullptr, editor.ConfigFile(), [&](int, int, const QString& label) {
			labels.push_back(label);
		});
	}

	// Landing nothing is the failure this pins, and it is invisible: the window builds exactly as
	// it does now and the screen sits on "Starting..." for the whole cold start.
	REQUIRE_FALSE(labels.empty());
	CHECK(labels.front() == QStringLiteral("Compiling shaders..."));

	// The shaders are one step because bgl builds every pipeline inside CreateGraphics; what
	// follows is the project, which reports per file.
	CHECK(std::ranges::count(labels, QStringLiteral("Compiling shaders...")) == 1);
}

TEST_CASE("What a project enables and an empty editor does not", "[mainwindow][render]")
{
	const HeadlessEditor editor;

	// The File menu reached the way a user does, rather than through MainWindow's own handle on it:
	// an entry that exists but never made it onto the bar would pass the other way round.
	const auto entry = [](const QMainWindow& window, const QString& text) -> const QAction* {
		for (const QAction* menu : window.menuBar()->actions())
		{
			if (menu->menu() == nullptr)
				continue;

			for (const QAction* action : menu->menu()->actions())
			{
				if (action->text() == text)
					return action;
			}
		}
		return nullptr;
	};

	// A whole menu rather than one entry: Edit and Window are greyed out as units, since neither
	// has anything to offer without a project.
	const auto menu = [](const QMainWindow& window, const QString& title) -> const QMenu* {
		for (const QAction* action : window.menuBar()->actions())
		{
			if (action->menu() != nullptr && action->menu()->title() == title)
				return action->menu();
		}
		return nullptr;
	};

	SECTION("with a project open")
	{
		const MainWindow window(nullptr, editor.ConfigFile());

		const QAction* save  = entry(window, "Save");
		const QAction* clean = entry(window, "Clean Unused Textures...");
		const QMenu*   edit  = menu(window, "Edit");
		const QMenu*   panes = menu(window, "Window");

		REQUIRE(save != nullptr);
		REQUIRE(clean != nullptr);
		REQUIRE(edit != nullptr);
		REQUIRE(panes != nullptr);

		CHECK(save->isEnabled());
		CHECK(clean->isEnabled());
		CHECK(edit->isEnabled());
		CHECK(panes->isEnabled());
	}

	SECTION("with none")
	{
		// The same config without a startupProject, so the window lands in its empty state. Both
		// entries act on a project, and enabled they would reach a null one.
		const fs::path config = editor.temp.path().toStdString() / fs::path("empty.json");
		core::file::write_atomic(config, R"({ "headless": true })");

		const MainWindow window(nullptr, config);

		const QAction* save  = entry(window, "Save");
		const QAction* clean = entry(window, "Clean Unused Textures...");
		const QMenu*   edit  = menu(window, "Edit");
		const QMenu*   panes = menu(window, "Window");

		REQUIRE(save != nullptr);
		REQUIRE(clean != nullptr);
		REQUIRE(edit != nullptr);
		REQUIRE(panes != nullptr);

		CHECK_FALSE(save->isEnabled());
		CHECK_FALSE(clean->isEnabled());

		// Window lists the docks, which are hidden here; Edit is empty either way.
		CHECK_FALSE(edit->isEnabled());
		CHECK_FALSE(panes->isEnabled());
	}
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

	const MainWindow window(nullptr, editor.ConfigFile());

	auto* animationDock = window.findChild<QDockWidget*>("AnimationEditorDock");
	auto* blendDock     = window.findChild<QDockWidget*>("BlendSpaceEditorDock");
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

	const MainWindow window(nullptr, editor.ConfigFile());

	auto* animation = window.findChild<AnimationEditorWindow*>();
	REQUIRE(animation != nullptr);

	auto* surfaces = animation->findChild<QTabWidget*>();
	REQUIRE(surfaces != nullptr);

	auto tabs = QStringList();
	for (int i = 0; i < surfaces->count(); ++i) tabs << surfaces->tabText(i);

	CHECK(tabs == QStringList({ QStringLiteral("Clip"), QStringLiteral("Blend") }));
}

TEST_CASE(
	"A blend set with nothing to show it on still opens, and still edits",
	"[mainwindow][blendspace][render]")
{
	const HeadlessEditor editor;
	const QString        key = WriteUnshownSet(editor.DataRoot());

	const MainWindow window(nullptr, editor.ConfigFile());

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

TEST_CASE("Leaving the Blend Space Editor's tab closes the set", "[mainwindow][blendspace][render]")
{
	const HeadlessEditor editor;
	const QString        key = WriteUnshownSet(editor.DataRoot());

	MainWindow window(nullptr, editor.ConfigFile());
	window.show();

	auto* animationDock = window.findChild<QDockWidget*>("AnimationEditorDock");
	auto* blendDock     = window.findChild<QDockWidget*>("BlendSpaceEditorDock");
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
