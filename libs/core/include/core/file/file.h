#pragma once

namespace core::file
{
	std::vector<std::byte>
	readFileBytes(const std::string& filePath);

	std::vector<std::byte>
	readFileBytes(std::string_view filePath);

	std::filesystem::path
	getLibraryPath();

}
