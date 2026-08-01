#pragma once

namespace core
{
	std::string
	get_executable_name() noexcept;

	// This process's id. The standard library has none; this is getpid/GetCurrentProcessId.
	[[nodiscard]] uint32_t
	process_id() noexcept;

	/**
	 * `path` with a leading `~` replaced by the user's home directory, or unchanged when it has none.
	 *
	 * A leading `~` is what a person writes in a config file and what no filesystem API expands.
	 * Returns `path` unchanged when the home directory cannot be determined.
	 */
	[[nodiscard]] std::filesystem::path
	expand_home(std::string_view path);
}
