#include "MainWindow.h"

#include "Windows/AnimationEditor/AnimationEditorWindow.h"
#include "Windows/MaterialEditor/MaterialEditorWindow.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "util/QtSupport.h"
#include "util/follows_project.h"
#include <assetlib/Project.h>

#include <QPointer>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <core/file/file.h>

// What a viewport's `headless` flag buys: a whole editor -- device, renderer, three viewports --
// standing in a test, so its construction and teardown are pinned rather than found by driving the
// app. What it draws is still bgl_tests' job.

namespace
{
	namespace fs = std::filesystem;

	// The panels that own a viewport today. The count below is what makes a fourth one loud.
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
			// temporalAA off in all three: a history costs a viewport nothing to skip here, and
			// these cases are about what is built, not what it accumulates.
			const std::string config = R"({
  "headless": true,
  "startupProject": ")" + EscapedProjectFile() +
			                           R"(",
  "levelEditor":     { "temporalAA": false },
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

TEST_CASE("Every viewport a headless editor builds is headless", "[mainwindow][render]")
{
	const HeadlessEditor editor;

	const MainWindow window(nullptr, editor.ConfigFile());

	const QList<RenderTargetWindow*> viewports = window.findChildren<RenderTargetWindow*>();

	// The Level Editor, the Material Editor's preview and the Animation Editor's. A panel added
	// later fails this line, which is the point: it then has to say whether it threads `headless`
	// through, rather than being window-backed in a suite that cannot realise a window.
	CHECK(static_cast<int>(viewports.size()) == c_ViewportCount);

	for (const RenderTargetWindow* view : viewports) CHECK(view->IsHeadless());
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
