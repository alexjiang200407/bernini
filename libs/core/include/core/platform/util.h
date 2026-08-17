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

	/**
	 * Flushes `path`'s contents to the storage device, so what was written survives a power loss and
	 * not merely a process crash. Closing a stream only hands the bytes to the OS cache.
	 *
	 * @return false if the file could not be opened or the flush failed. Reported rather than
	 *         thrown, because only the caller knows whether losing the write matters.
	 */
	[[nodiscard]] bool
	sync_file(const std::filesystem::path& path) noexcept;

	/**
	 * The directory-entry counterpart of sync_file: makes a rename into `directory` durable.
	 *
	 * On POSIX a renamed file's contents can be durable while the entry naming it is not, so both
	 * are needed. On Win32 the rename is journaled and a directory handle cannot be flushed, so
	 * this succeeds without doing anything.
	 */
	[[nodiscard]] bool
	sync_directory(const std::filesystem::path& directory) noexcept;
}
