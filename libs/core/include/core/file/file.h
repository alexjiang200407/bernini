#pragma once

namespace core::file
{
	std::vector<std::byte>
	read_file_bytes(const std::string& filePath);

	std::vector<std::byte>
	read_file_bytes(std::string_view filePath);

	std::filesystem::path
	get_library_path();

}
