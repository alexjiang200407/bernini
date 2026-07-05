#include <core/file/file.h>

namespace core::file
{
	std::vector<std::byte>
	readFileBytes(const std::string& filePath)
	{
		std::ifstream fileStream{ filePath, std::ios::binary | std::ios::ate };
		if (!fileStream)
		{
			throw std::runtime_error("Failed to open file: " + filePath);
		}
		std::streamsize fileSize = fileStream.tellg();
		fileStream.seekg(0, std::ios::beg);
		std::vector<std::byte> buffer(static_cast<uint64_t>(fileSize));
		if (!fileStream.read(reinterpret_cast<char*>(buffer.data()), fileSize))
		{
			throw std::runtime_error("Failed to open file: " + filePath);
		}
		return buffer;
	}

	std::vector<std::byte>
	readFileBytes(std::string_view filePath)
	{
		return readFileBytes(std::string{ filePath });
	}
}
