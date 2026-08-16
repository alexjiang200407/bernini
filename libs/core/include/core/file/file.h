#pragma once

namespace core::file
{
	std::vector<std::byte>
	read_file_bytes(const std::string& filePath);

	std::vector<std::byte>
	read_file_bytes(std::string_view filePath);

	/**
	 * FNV-1a over a file's contents, read in fixed-size chunks so a file larger than memory still
	 * hashes. Equal to hash_bytes over the whole file read at once.
	 *
	 * @return Nullopt if the file cannot be opened, or if a read fails partway through it.
	 */
	[[nodiscard]] std::optional<uint64_t>
	hash_file(const std::filesystem::path& filePath);

	std::filesystem::path
	get_library_path();

	// The running executable's own path. Unlike get_library_path this does not move with the
	// binary that core was linked into, which on a build staging libraries and executables into
	// separate directories is a different place entirely.
	std::filesystem::path
	get_executable_path();

}
