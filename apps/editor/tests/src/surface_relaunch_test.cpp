#include "util/surface_relaunch.h"

#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <core/file/file.h>

#include <filesystem>
#include <system_error>

// Opening a project reaches a modal confirmation MainWindow gives no test a way past, so the decision
// behind it is a free function and these drive it directly.

namespace
{
	namespace fs = std::filesystem;

	struct Shaders
	{
		QTemporaryDir temp;

		[[nodiscard]] fs::path
		Directory(const char* project) const
		{
			const fs::path dir = temp.path().toStdString() / fs::path(project) / "Shaders";
			fs::create_directories(dir);
			return dir;
		}

		[[nodiscard]] fs::path
		WithSurface(const char* project) const
		{
			const fs::path dir = Directory(project);
			core::file::write_atomic(dir / "Toon.slang", "// a surface\n");
			return dir;
		}
	};
}

TEST_CASE("A project whose shaders the session registered opens in place", "[surfacerelaunch]")
{
	const Shaders  shaders;
	const fs::path dir = shaders.WithSurface("Game");

	CHECK_FALSE(editor::OpeningNeedsRelaunch(dir, 2, dir));

	// The test project a feature checkout reaches through a symlink is the same directory as the one
	// config.json names by its real path.
	std::error_code ec;
	const fs::path  link = fs::path(shaders.temp.path().toStdString()) / "Linked";
	fs::create_directory_symlink(dir.parent_path(), link, ec);
	if (!ec)
		CHECK_FALSE(editor::OpeningNeedsRelaunch(dir, 2, link / "Shaders"));
}

TEST_CASE(
	"A project with shaders restarts a session that registered none from it",
	"[surfacerelaunch]")
{
	const Shaders shaders;

	CHECK(editor::OpeningNeedsRelaunch(shaders.Directory("Empty"), 0, shaders.WithSurface("Game")));

	// Started with no project at all, which is the blank startupProject case.
	CHECK(editor::OpeningNeedsRelaunch(fs::path(), 0, shaders.WithSurface("Game")));
}

TEST_CASE(
	"A project without shaders restarts a session holding another project's surfaces",
	"[surfacerelaunch]")
{
	const Shaders shaders;

	// Otherwise the Output selector keeps offering surfaces this project has no file for.
	CHECK(editor::OpeningNeedsRelaunch(shaders.WithSurface("Game"), 1, shaders.Directory("Empty")));
}

TEST_CASE("Projects without shaders share a session", "[surfacerelaunch]")
{
	const Shaders  shaders;
	const fs::path other = shaders.Directory("Other");
	core::file::write_atomic(other / "README.md", "not a module\n");

	CHECK_FALSE(editor::OpeningNeedsRelaunch(shaders.Directory("Empty"), 0, other));
	CHECK_FALSE(editor::OpeningNeedsRelaunch(fs::path(), 0, shaders.Directory("Empty")));
}
