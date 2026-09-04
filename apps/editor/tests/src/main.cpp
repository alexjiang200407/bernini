#include "util/qt_platform.h"

#include <QApplication>
#include <QtGlobal>
#include <catch2/catch_session.hpp>
#include <core/err/util.h>
#include <core/profiling/MemoryReport.h>

namespace
{
	bool g_PlatformWasNamed = false;

	// Matched whole, as Qt matches it: `-platformpluginpath` and `-platformtheme` are switches of
	// their own and name no platform, so a prefix test would quietly excuse the [platform] case.
	bool
	NamesAPlatform(int argc, char* const argv[]) noexcept
	{
		for (int i = 1; i < argc; ++i)
		{
			if (std::string_view(argv[i]) == "-platform")
				return true;
		}

		return false;
	}

	/**
	 * Takes `--mem-report <path>` out of argv and returns it, leaving `args` as the command line
	 * Catch2 sees. Catch2 rejects an option it does not know, so this cannot simply be read past.
	 */
	std::filesystem::path
	TakeMemoryReportPath(std::vector<char*>& args)
	{
		for (std::size_t i = 1; i + 1 < args.size(); ++i)
		{
			if (std::string_view(args[i]) != "--mem-report")
				continue;

			const std::filesystem::path path = args[i + 1];
			args.erase(
				args.begin() + static_cast<std::ptrdiff_t>(i),
				args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
			return path;
		}

		return {};
	}
}

namespace editor::test
{
	bool
	PlatformWasNamed() noexcept
	{
		return g_PlatformWasNamed;
	}
}

/**
 * A QApplication, not a QCoreApplication: the tests construct real widgets, and QPixmap needs a
 * platform plugin. Qt consumes its own switches (`-platform offscreen`) out of argv before Catch2
 * sees what is left, so the two command lines do not collide.
 */
int
main(int argc, char* argv[])
{
	// First thing, before a widget or a device exists: a crash here otherwise leaves nothing behind,
	// and this suite stands up both. It also keeps a CRT assertion from opening a modal dialog that a
	// sharded or CI run would wait on forever.
	core::install_crash_handlers();

	// Read before the default below is written, so a run that named a platform stays
	// distinguishable from one that was handed the default.
	g_PlatformWasNamed =
		!qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") || NamesAPlatform(argc, argv);

	// Offscreen by default, so the widgets the tests stand up never take focus off whatever is
	// being worked on. A default only: a named platform outranks it, so the windows can still be
	// watched. Guarded by the define set alongside the CMake that establishes the plugin is
	// reachable -- naming a platform Qt cannot load aborts with a modal error box.
#if defined(EDITOR_TESTS_HAVE_OFFSCREEN)
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		qputenv("QT_QPA_PLATFORM", "offscreen");
#endif

	// This suite stands up a real device and loads real containers, which makes it the one binary
	// that can price them without a person driving a window. Armed on request rather than always:
	// it has no log file, so the table would land in the terminal of every `just test`.
	auto args = std::vector<char*>(argv, argv + argc);

	// Constructed only when armed: the guard logs its table from the destructor whether or not it
	// was given a JSON path, so an unconditional one would print it after every `just test`.
	const std::filesystem::path reportPath   = TakeMemoryReportPath(args);
	auto                        memoryReport = std::optional<core::profiling::MemoryReport>();
	if (!reportPath.empty())
		memoryReport.emplace(reportPath);

	// QApplication takes argc by non-const reference and consumes its own switches out of it, so
	// `count` is what is left for Catch2 -- exactly as it was when Qt edited the real argv.
	int          count = static_cast<int>(args.size());
	QApplication app(count, args.data());

	return Catch::Session().run(count, args.data());
}
