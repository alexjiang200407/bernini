#include "util/qt_platform.h"

#include <QGuiApplication>
#include <catch2/catch_test_macros.hpp>
#include <qlatin1stringview.h>

// The suite constructs real widgets, and some of them show themselves -- the loading screens
// util/Modal.h drives, the importer dialog. On a platform that presents, those land on the desktop
// and take focus off whatever the suite was run to check. What keeps them off it is `main`
// defaulting to `offscreen`, which is a compile-time default and so is exactly the kind of thing a
// build reshuffle drops silently.

TEST_CASE("The suite runs offscreen when no platform was named", "[platform]")
{
	if (editor::test::PlatformWasNamed())
	{
		SUCCEED("a platform was named for this run, so the default under test did not apply");
		return;
	}

	CHECK(QGuiApplication::platformName() == QLatin1String("offscreen"));
}
