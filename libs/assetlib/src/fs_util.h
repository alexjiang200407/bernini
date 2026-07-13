#pragma once

namespace assetlib
{
	/**
	 * Creates `dir` and any missing parent, doing nothing if it already exists.
	 *
	 * @throws std::runtime_error naming the OS's reason -- permission denied, read-only volume, name too
	 *         long -- if the directory does not exist and cannot be created. Swallowing that failure
	 *         only defers it: the cook then dies inside an encoder, blaming a file whose directory was
	 *         never there.
	 */
	void
	createDirectories(const std::filesystem::path& dir);

	/**
	 * Message for a std::ofstream that would not open or write `path`, ending in the OS's reason:
	 * "permission denied" for a read-only or locked file, "no such file or directory" for a missing
	 * parent. The stream itself reports only that it failed, and that is not something a user can act
	 * on.
	 *
	 * Reads errno, which the standard streams set on a failed open, so call it before anything else
	 * clobbers errno -- and set `errno = 0` before the open, since a stale value from an unrelated call
	 * would otherwise be blamed.
	 */
	[[nodiscard]] std::string
	fileErrorMessage(std::string_view what, const std::filesystem::path& path);
}
