#include "util/recent_projects.h"

#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <core/file/file.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

	struct RecentList
	{
		QTemporaryDir temp;

		[[nodiscard]] fs::path
		Root() const
		{
			return fs::weakly_canonical(fs::path(temp.path().toStdWString()));
		}

		[[nodiscard]] fs::path
		File() const
		{
			return editor::RecentProjectsFileBeside(Root() / "config.json");
		}

		// Only the file's existence is read, so a project here is an empty .bproj.
		[[nodiscard]] fs::path
		Project(const std::string& name) const
		{
			const fs::path file = Root() / name / (name + ".bproj");
			fs::create_directories(file.parent_path());
			core::file::write_atomic(file, "{}");
			return file;
		}

		[[nodiscard]] std::vector<fs::path>
		Read() const
		{
			return editor::ReadRecentProjects(File());
		}
	};
}

TEST_CASE("The list lives beside the config it belongs to", "[recentprojects]")
{
	CHECK(
		editor::RecentProjectsFileBeside(fs::path("a") / "b" / "config.json") ==
		fs::path("a") / "b" / "recent_projects.json");
}

TEST_CASE("A missing or unreadable list is an empty one", "[recentprojects]")
{
	const RecentList list;
	CHECK(list.Read().empty());

	core::file::write_atomic(list.File(), "not json");
	CHECK(list.Read().empty());
}

TEST_CASE("The project opened last comes first", "[recentprojects]")
{
	const RecentList list;
	const fs::path   a = list.Project("A");
	const fs::path   b = list.Project("B");

	editor::RecordRecentProject(list.File(), a);
	editor::RecordRecentProject(list.File(), b);

	CHECK(list.Read() == std::vector<fs::path>{ b, a });
}

TEST_CASE("Opening a listed project again moves it to the top", "[recentprojects]")
{
	const RecentList list;
	const fs::path   a = list.Project("A");
	const fs::path   b = list.Project("B");

	editor::RecordRecentProject(list.File(), a);
	editor::RecordRecentProject(list.File(), b);
	editor::RecordRecentProject(list.File(), a);

	CHECK(list.Read() == std::vector<fs::path>{ a, b });

	// The same project reached through another spelling is still the same entry.
	editor::RecordRecentProject(list.File(), b.parent_path() / ".." / "B" / "B.bproj");
	CHECK(list.Read() == std::vector<fs::path>{ b, a });
}

TEST_CASE("The list keeps only the most recent projects", "[recentprojects]")
{
	const RecentList list;

	auto opened = std::vector<fs::path>();
	for (std::size_t i = 0; i < editor::c_MaxRecentProjects + 3; ++i)
	{
		opened.emplace_back(list.Project("P" + std::to_string(i)));
		editor::RecordRecentProject(list.File(), opened.back());
	}

	const std::vector<fs::path> read = list.Read();
	REQUIRE(read.size() == editor::c_MaxRecentProjects);
	CHECK(read.front() == opened.back());
	CHECK(read.back() == opened[3]);
}

// A feature checkout's test project goes with `ws done`, and the landing page must not offer it.
TEST_CASE("A project that is gone is not listed", "[recentprojects]")
{
	const RecentList list;
	const fs::path   a = list.Project("A");
	const fs::path   b = list.Project("B");

	editor::RecordRecentProject(list.File(), a);
	editor::RecordRecentProject(list.File(), b);

	std::error_code ec;
	fs::remove_all(b.parent_path(), ec);
	REQUIRE_FALSE(ec);

	CHECK(list.Read() == std::vector<fs::path>{ a });
}
