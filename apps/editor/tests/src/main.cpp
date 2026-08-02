#include <QApplication>
#include <QtGlobal>
#include <catch2/catch_session.hpp>
#include <core/err/util.h>

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

	// Offscreen by default, so the widgets the tests stand up never take focus off whatever is
	// being worked on. A default only: `-platform windows` outranks the environment, so the
	// windows can still be watched. Guarded by the define set alongside the CMake that deploys
	// the plugin -- naming a platform Qt cannot load aborts with a modal error box.
#if defined(EDITOR_TESTS_HAVE_OFFSCREEN)
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		qputenv("QT_QPA_PLATFORM", "offscreen");
#endif

	QApplication app(argc, argv);

	return Catch::Session().run(argc, argv);
}
