#include <core/file/file.h>

#include <core/hash.h>

namespace core::file
{
	namespace
	{
		constexpr size_t c_HashChunkBytes = 64 * 1024;
	}

	std::vector<std::byte>
	read_file_bytes(const std::string& filePath)
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
	read_file_bytes(std::string_view filePath)
	{
		return read_file_bytes(std::string{ filePath });
	}

	std::optional<uint64_t>
	hash_file(const std::filesystem::path& filePath)
	{
		std::ifstream fileStream{ filePath, std::ios::binary };
		if (!fileStream)
			return std::nullopt;

		std::vector<char> chunk(c_HashChunkBytes);
		uint64_t          hash = hash_seed();

		while (fileStream.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) ||
		       fileStream.gcount() != 0)
		{
			hash = hash_bytes(chunk.data(), static_cast<size_t>(fileStream.gcount()), hash);
		}

		// eof is how a complete read ends; anything else left the file partly unread, and a hash of
		// the prefix would compare equal to nothing meaningful.
		if (!fileStream.eof())
			return std::nullopt;

		return hash;
	}
}
