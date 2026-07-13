#include <QApplication>
#include <catch2/catch_session.hpp>

/**
 * A QApplication, not a QCoreApplication: the tests construct real widgets, and QPixmap needs a
 * platform plugin. Qt consumes its own switches (`-platform offscreen`) out of argv before Catch2
 * sees what is left, so the two command lines do not collide.
 */
int
main(int argc, char* argv[])
{
	QApplication app(argc, argv);

	return Catch::Session().run(argc, argv);
}
