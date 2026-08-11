#include "util/window_title.h"

#include <catch2/catch_test_macros.hpp>

#include "util/QtSupport.h"

TEST_CASE("A checkout that names no instance titles its window as it always did", "[windowtitle]")
{
	CHECK(editor::WindowTitle(QString(), QString()) == "Bernini Editor");
	CHECK(editor::WindowTitle(QString(), "Test Project") == "Bernini Editor — Test Project");
}

TEST_CASE("The instance name leads the title, ahead of what two editors share", "[windowtitle]")
{
	// The point of the key: A and B run the same build of the same project, so everything after the
	// instance name is identical between them and a truncated window list shows only the tail.
	CHECK(editor::WindowTitle("A", "Test Project") == "A — Bernini Editor — Test Project");
	CHECK(editor::WindowTitle("B", "Test Project") == "B — Bernini Editor — Test Project");
}

TEST_CASE("An instance with no project open still says which editor it is", "[windowtitle]")
{
	CHECK(editor::WindowTitle("A", QString()) == "A — Bernini Editor");
}

TEST_CASE("A blank instance name is no instance name", "[windowtitle]")
{
	// A key left as "" or filled in with a stray space must not leave the title starting on a dash.
	CHECK(editor::WindowTitle("   ", "Test Project") == "Bernini Editor — Test Project");
	CHECK(editor::WindowTitle("  A  ", "Test Project") == "A — Bernini Editor — Test Project");
}
