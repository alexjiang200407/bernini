#include "MainWindow.h"
#include "Plugins/plugin_loader.h"
#include "Windows/MeshEditor/MeshEditorWindow.h"
#include "Windows/MeshEditor/MeshPreviewWindow.h"
#include "Windows/MeshEditor/TextureUploads.h"
#include "util/QtSupport.h"  // IWYU pragma: keep

#include <QDockWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <qtestsupport_core.h>

#include <algorithm>
#include <assetlib/Project.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <core/file/file.h>
#include <core/platform/util.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <qnamespace.h>
#include <qobject.h>
#include <ratio>
#include <string>
#include <utility>

namespace
{
	namespace fs = std::filesystem;

	using Clock = std::chrono::steady_clock;

	double
	MsSince(Clock::time_point start)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	}

	/**
	 * The longest the GUI thread went without returning to an event loop -- the outer one or a
	 * loading screen's -- while it was armed, measured between the ticks of a 1 ms timer. What a
	 * user feels as a frozen editor, whatever the cause.
	 */
	class GuiStalls
	{
	public:
		GuiStalls()
		{
			m_Timer.setTimerType(Qt::PreciseTimer);
			m_Timer.setInterval(1);
			QObject::connect(&m_Timer, &QTimer::timeout, [this] {
				const auto now   = Clock::now();
				const auto gapMs = std::chrono::duration<double, std::milli>(now - m_Last).count();
				m_Last           = now;
				m_LongestMs      = std::max(m_LongestMs, gapMs);
				if (gapMs > c_FrameMs)
					++m_OverAFrame;
			});
		}

		void
		Start()
		{
			m_LongestMs  = 0.0;
			m_OverAFrame = 0;
			m_Last       = Clock::now();
			m_Timer.start();
		}

		void
		Stop()
		{
			m_Timer.stop();
		}

		[[nodiscard]] double
		LongestMs() const noexcept
		{
			return m_LongestMs;
		}

		[[nodiscard]] int
		OverAFrame() const noexcept
		{
			return m_OverAFrame;
		}

	private:
		static constexpr double c_FrameMs = 1000.0 / 60.0;

		QTimer            m_Timer;
		Clock::time_point m_Last;
		double            m_LongestMs  = 0.0;
		int               m_OverAFrame = 0;
	};
}

// How long opening a mesh in the Mesh Editor keeps the GUI thread from its event loop, as a drop
// onto the preview does once the mesh is imported: the `.bmesh` under the data root
// `BERNINI_TEST_PROJECT` names, at the key `BERNINI_MESH_OPEN` names. Not a test of behaviour and not
// runnable in CI -- run it by hand, and read the numbers off the warnings it prints:
//
//   BERNINI_TEST_PROJECT=<Data root> BERNINI_MESH_OPEN=<key> just run editor_tests -- "[.meshopen]"
//
// The preview reads the editor's deployed config.json, so it is lit and sized as the editor's is;
// set BERNINI_MESH_OPEN_WINDOWED and pass `-platform cocoa` (or `windows`) to draw into real windows,
// whose frames pace the render thread as a user's do.
//
// It opens the mesh twice, so the second open is the warm one a user sits in. A headless editor opens
// the project the data root belongs to and may write to it as any editor would, so point it at a
// copy.
TEST_CASE("Opening a mesh in the Mesh Editor, timed", "[.meshopen][render]")
{
	const std::optional<std::string> root = core::env_var("BERNINI_TEST_PROJECT");
	const std::optional<std::string> key  = core::env_var("BERNINI_MESH_OPEN");
	if (!root.has_value() || !key.has_value())
	{
		SKIP("BERNINI_TEST_PROJECT or BERNINI_MESH_OPEN is not set");
	}
	const auto dataRoot = fs::path(*root);
	const auto mesh     = dataRoot / *key;
	REQUIRE(fs::exists(mesh));

	auto projectFile = fs::path();
	for (const auto& entry : fs::directory_iterator(dataRoot.parent_path()))
		if (entry.path().extension() == assetlib::Project::c_FileExtension)
			projectFile = entry.path();
	REQUIRE_FALSE(projectFile.empty());

	const QTemporaryDir temp;
	const auto          config = fs::path(temp.path().toStdString()) / "config.json";
	// The deployed config, so the preview is lit and drawn as the editor draws it; headless unless
	// BERNINI_MESH_OPEN_WINDOWED is set, which wants `-platform cocoa` (or `windows`) beside it.
	auto settings = nlohmann::json::parse(
		std::ifstream(core::file::get_executable_path().parent_path() / "config.json"));
	settings["headless"] = !core::env_var("BERNINI_MESH_OPEN_WINDOWED").has_value();
	settings.erase("startupProject");
	core::file::write_atomic(config, settings.dump(2));

	auto plugins =
		std::make_unique<editor::plugins::PluginSession>(editor::plugins::PluginSession::Load(
			editor::plugins::ConfiguredPluginDirectories(config),
			editor::plugins::CurrentBuildIdentity(),
			fs::path(temp.path().toStdString()) / "plugin-copies"));

	MainWindow window(std::move(plugins), assetlib::Project::Open(projectFile), config);
	window.show();

	auto* dock    = window.findChild<QDockWidget*>("bernini.material");
	auto* panel   = window.findChild<MeshEditorWindow*>();
	auto* preview = window.findChild<MeshPreviewWindow*>();
	auto* uploads = window.findChild<TextureUploads*>();
	REQUIRE(dock != nullptr);
	REQUIRE(panel != nullptr);
	REQUIRE(preview != nullptr);
	REQUIRE(uploads != nullptr);

	dock->raise();
	REQUIRE(editor::test::WaitFor([dock] { return dock->isVisible(); }));

	GuiStalls stalls;
	for (const char* const pass : { "cold", "warm" })
	{
		stalls.Start();
		const auto start = Clock::now();

		preview->LoadMesh(mesh);
		const double shownMs = MsSince(start);
		REQUIRE(preview->MeshPath() == mesh);

		REQUIRE(editor::test::WaitFor([uploads] { return uploads->IsIdle(); }, 300000));
		const double mapsMs = MsSince(start);

		// The texture nodes' tiles decode on a pool of their own, and land within this.
		QTest::qWait(2000);
		stalls.Stop();

		WARN(
			"mesh open (" << pass << "): shown " << shownMs << " ms, every map uploaded " << mapsMs
						  << " ms; the GUI thread's longest stall " << stalls.LongestMs() << " ms, "
						  << stalls.OverAFrame() << " stalls over a frame");

		panel->Reset();
		REQUIRE(editor::test::WaitFor([preview] { return preview->MeshPath().empty(); }));
	}
}
