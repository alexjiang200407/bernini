#include <core/err/util.h>
#include <core/file/file.h>
#include <core/platform/util.h>

namespace core::file
{
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
					std::strerror(errno));

			out.write(
				reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
			out.close();

			if (!out)
			{
				// Read before the remove below, whose own unlink would overwrite errno on failure.
				const std::string reason = std::strerror(errno);

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
