#include "util/panel_visibility.h"

#include "util/QtSupport.h"  // IWYU pragma: keep

#include <QWidget>
#include <catch2/catch_test_macros.hpp>

// Qt reports a dock invisible for two unrelated reasons -- its tab was deselected, and the window it
// lives in went away -- and only the first is the user leaving the panel. Measured on Qt 6.8.3:
// minimizing emits visibilityChanged(false) for every dock with the window still isVisible(), so
// neither test below stands in for the other.

TEST_CASE("A shown dock is the shown panel", "[panelclear]")
{
	QWidget window;
	window.show();
	REQUIRE(editor::test::WaitFor([&window] { return window.isVisible(); }));

	CHECK(editor::IsPanelShown(true, &window));
}

TEST_CASE("A deselected tab leaves the panel", "[panelclear]")
{
	QWidget window;
	window.show();
	REQUIRE(editor::test::WaitFor([&window] { return window.isVisible(); }));

	CHECK_FALSE(editor::IsPanelShown(false, &window));
}

TEST_CASE("A minimized window is not the user leaving the panel", "[panelclear]")
{
	QWidget window;
	window.show();
	REQUIRE(editor::test::WaitFor([&window] { return window.isVisible(); }));

	window.showMinimized();
	REQUIRE(window.isMinimized());

	CHECK(editor::IsPanelShown(false, &window));
}

TEST_CASE("A hidden window is not either", "[panelclear]")
{
	QWidget window;
	window.show();
	REQUIRE(editor::test::WaitFor([&window] { return window.isVisible(); }));

	window.hide();
	REQUIRE_FALSE(window.isVisible());

	CHECK(editor::IsPanelShown(false, &window));
}

TEST_CASE("With no window to ask, nothing is thrown away", "[panelclear]")
{
	CHECK(editor::IsPanelShown(false, nullptr));
}
