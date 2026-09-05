#include <algorithm>
#include <atomic>
#include <cerrno>
#include <core/err/util.h>
#include <core/file/IFileSystem.h>
#include <core/file/file.h>
#include <core/platform/util.h>

#include <core/hash.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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

	std::vector<std::byte>
	read_file_bytes(const std::filesystem::path& filePath)
	{
		return read_file_bytes(filePath.string());
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

	std::optional<uint64_t>
	hash_file(const IFileSystem& fileSystem, std::string_view path)
	{
		const std::optional<FileStamp> stamp = fileSystem.Stat(path);
		if (!stamp.has_value())
			return std::nullopt;

		uint64_t hash = hash_seed();

		// ReadRange reports a failure by throwing, where the path overload has a stream to ask -- so
		// the contract the two share, nullopt for a file that could not be read whole, is restored
		// here rather than let through.
		try
		{
			for (uint64_t offset = 0; offset < stamp->size;)
			{
				const uint64_t taken = std::min<uint64_t>(c_HashChunkBytes, stamp->size - offset);
				const std::vector<std::byte> chunk = fileSystem.ReadRange(path, offset, taken);

				hash = hash_bytes(chunk.data(), chunk.size(), hash);
				offset += taken;
			}
		}
		catch (const std::exception&)
		{
			return std::nullopt;
		}

		return hash;
	}

	void
	write_atomic(const std::filesystem::path& path, std::span<const std::byte> bytes)
	{
		static std::atomic<uint32_t> g_Counter = 0;

		const std::filesystem::path tmp = std::format(
			"{}.{}.{}.tmp",
			path.string(),
			core::process_id(),
			g_Counter.fetch_add(1, std::memory_order_relaxed));

		{
			// Cleared so the message cannot blame a stale errno from an unrelated call.
			errno = 0;
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out)
				core::throw_runtime_error(
					"write_atomic: cannot open '{}': {}",
					tmp.string(),
					std::generic_category().message(errno));

			out.write(
				reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
			out.close();

			if (!out)
			{
				// Read before the remove below, whose own unlink would overwrite errno on failure.
				const std::string reason = std::generic_category().message(errno);

				std::error_code ec;
				std::filesystem::remove(tmp, ec);
				core::throw_runtime_error(
					"write_atomic: failed to write '{}': {}",
					tmp.string(),
					reason);
			}
		}

		// Closing the stream only hands the bytes to the OS cache. Without this the rename can
		// commit a name whose contents a power loss has not yet reached the device -- which is the
		// truncated-but-plausible file this function exists to make impossible.
		if (!core::sync_file(tmp))
		{
			std::error_code ec;
			std::filesystem::remove(tmp, ec);
			core::throw_runtime_error("write_atomic: cannot flush '{}'", tmp.string());
		}

		std::error_code ec;
		std::filesystem::rename(tmp, path, ec);
		if (ec)
		{
			std::error_code removeEc;
			std::filesystem::remove(tmp, removeEc);
			core::throw_runtime_error(
				"write_atomic: cannot commit '{}': {}",
				path.string(),
				ec.message());
		}

		// A durable file under a directory entry that is not durable is still a lost write. Failure
		// here is not fatal: the rename has happened, and what is at risk is only its ordering
		// against an unrelated crash.
		(void)core::sync_directory(path.parent_path());
	}
}
