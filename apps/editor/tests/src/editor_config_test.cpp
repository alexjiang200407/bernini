#include "util/editor_config.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

// The memory report is armed in main, before any window exists, so nothing about it can be driven
// through MainWindow. What decides it is a free function taking a path, and these are that.

namespace
{
	struct ConfigFile
	{
		std::filesystem::path path;

		explicit ConfigFile(const char* name, const std::string& contents) :
			path(std::filesystem::temp_directory_path() / name)
		{
			std::ofstream(path) << contents;
		}

		~ConfigFile()
		{
			std::error_code ec;
			std::filesystem::remove(path, ec);
		}
	};
}

TEST_CASE("A config that turns the memory report off is obeyed", "[editorconfig]")
{
	const ConfigFile config("bernini_editor_config_off.json", R"({ "memoryReport": false })");

	CHECK_FALSE(editor::MemoryReportEnabled(config.path));
}

TEST_CASE("A config that says nothing about the memory report leaves it on", "[editorconfig]")
{
	const ConfigFile config("bernini_editor_config_silent.json", R"({ "headless": false })");

	// Every config written before this key existed is this case, and each of them wants the report.
	CHECK(editor::MemoryReportEnabled(config.path));
}

TEST_CASE("A missing or unreadable config leaves the memory report on", "[editorconfig]")
{
	// Losing a diagnostic because a config could not be parsed is the worse of the two failures.
	CHECK(
		editor::MemoryReportEnabled(
			std::filesystem::temp_directory_path() / "bernini_no_such_config.json"));

	const ConfigFile broken("bernini_editor_config_broken.json", "{ not json");
	CHECK(editor::MemoryReportEnabled(broken.path));
}
