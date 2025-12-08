#include <Core/except/BerniniException.h>
#include <Core/file/file.h>

namespace core::file
{
	std::vector<std::byte>
	readFileBytes(const std::string& filePath)
	{
		std::ifstream fileStream{ filePath, std::ios::binary | std::ios::ate };
		if (!fileStream)
		{
			throw core::except::BerniniException{ "Open File Error",
				                                  "Failed to open file: " + filePath };
		}
		std::streamsize fileSize = fileStream.tellg();
		fileStream.seekg(0, std::ios::beg);
		std::vector<std::byte> buffer(fileSize);
		if (!fileStream.read(reinterpret_cast<char*>(buffer.data()), fileSize))
		{
			throw core::except::BerniniException{ "Open File Error",
				                                  "Failed to read file: " + filePath };
		}
		return buffer;
	}
}
