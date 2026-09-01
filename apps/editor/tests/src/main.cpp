#include "util/qt_platform.h"

#include <QApplication>
#include <QtGlobal>
#include <catch2/catch_session.hpp>
#include <core/err/util.h>

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

	QApplication app(argc, argv);

	return Catch::Session().run(argc, argv);
}
