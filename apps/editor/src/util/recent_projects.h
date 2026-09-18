#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace editor
{
	inline constexpr std::size_t c_MaxRecentProjects = 10;

	/**
	 * Where the recent-projects list for the editor reading `configPath` lives: beside it, so it is
	 * one list per checkout, and a test that names its own config gets its own list.
	 */
	[[nodiscard]] std::filesystem::path
	RecentProjectsFileBeside(const std::filesystem::path& configPath);

	/**
	 * The `.bproj` files in the list at `listFile`, most recent first. An entry whose file no longer
	 * exists is left out, and a list that is missing or cannot be parsed reads as empty.
	 */
	[[nodiscard]] std::vector<std::filesystem::path>
	ReadRecentProjects(const std::filesystem::path& listFile);

	/**
	 * Moves `projectFile` to the front of the list at `listFile`, adding it if it is not there, and
	 * drops the oldest entries past `c_MaxRecentProjects`.
	 *
	 * Never throws: a list that cannot be written loses the landing page an entry, and that is no
	 * reason to fail the open that caused it.
	 */
	void
	RecordRecentProject(
		const std::filesystem::path& listFile,
		const std::filesystem::path& projectFile) noexcept;
}
