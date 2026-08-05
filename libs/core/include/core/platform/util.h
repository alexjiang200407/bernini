#pragma once

namespace core
{
	std::string
	get_executable_name() noexcept;

	// This process's id. The standard library has none; this is getpid/GetCurrentProcessId.
	[[nodiscard]] uint32_t
	process_id() noexcept;

	/**
	 * The environment variable `name`, or nullopt when it is not set.
	 *
	 * `std::getenv` is deprecated on MSVC, where a warning is an error, so reaching for it directly
	 * does not compile on every platform this builds for.
	 */
	[[nodiscard]] std::optional<std::string>
	env_var(const char* name);

	/**
	 * `path` with a leading `~` replaced by the user's home directory, or unchanged when it has none.
	 *
	 * A leading `~` is what a person writes in a config file and what no filesystem API expands.
	 * Returns `path` unchanged when the home directory cannot be determined.
	 */
	[[nodiscard]] std::filesystem::path
	expand_home(std::string_view path);
}
