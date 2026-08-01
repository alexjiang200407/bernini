#pragma once

namespace core::file
{
	std::vector<std::byte>
	read_file_bytes(const std::string& filePath);

	std::vector<std::byte>
	read_file_bytes(std::string_view filePath);

	std::filesystem::path
	get_library_path();

	// The running executable's own path. Unlike get_library_path this does not move with the
	// binary that core was linked into, which on a build staging libraries and executables into
	// separate directories is a different place entirely.
	std::filesystem::path
	get_executable_path();

}
