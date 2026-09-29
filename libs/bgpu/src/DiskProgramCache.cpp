#include "DiskProgramCache.h"
#include "SlangSessions.h"
#include <algorithm>
#include <bgpu/ProgramCache.h>
#include <core/file/file.h>
#include <core/hash.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <mutex>
#include <span>
#include <spdlog/spdlog.h>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace bgpu
{
	namespace
	{
		// "BSC2": a header of this shape, then the owner's bytes. A file without it -- one written
		// before the store checked its entries -- is a miss.
		constexpr uint32_t c_EntryMagic = 0x32435342u;

		std::vector<std::filesystem::path>
		SourceFiles(const std::vector<std::string>& searchPaths)
		{
			namespace fs = std::filesystem;

			std::vector<fs::path> files;
			for (const std::string& root : searchPaths)
			{
				std::error_code ec;
				if (!fs::exists(root, ec))
					continue;

				for (auto it = fs::recursive_directory_iterator(root, ec);
				     !ec && it != fs::recursive_directory_iterator();
				     it.increment(ec))
				{
					if (it->is_regular_file(ec))
						files.push_back(it->path());
				}
			}

			// By path, so the salt does not depend on the order a walk visits them in.
			std::ranges::sort(files);
			return files;
		}
	}

	DiskProgramCache::DiskProgramCache(
		std::filesystem::path directory,
		const SlangSessions&  sessions) : m_Directory(std::move(directory)), m_Sessions(sessions)
	{
		std::error_code ec;
		std::filesystem::create_directories(m_Directory, ec);
		if (ec)
		{
			spdlog::warn(
				"Could not create the shader cache directory {}: {}; nothing will be cached",
				m_Directory.string(),
				ec.message());
		}
	}

	uint64_t
	DiskProgramCache::ContextSalt() const
	{
		std::call_once(m_ContextSaltOnce, [this] {
			uint64_t salt = core::hash_string(m_Sessions.GetOptionsSalt(), core::hash_seed());
			for (const std::filesystem::path& file : SourceFiles(m_Sessions.GetSearchPaths()))
			{
				const std::string            path  = file.generic_string();
				const std::vector<std::byte> bytes = core::file::read_file_bytes(path);
				salt                               = core::hash_string(path, salt);
				salt = core::hash_bytes(bytes.data(), bytes.size(), salt);
			}
			m_ContextSalt = salt;
		});
		return m_ContextSalt;
	}

	uint64_t
	DiskProgramCache::ComputeKey(
		const ProgramCacheOwner&                         owner,
		std::vector<std::pair<std::string, std::string>> moduleEntries) const
	{
		uint64_t key = core::hash_pod(m_Sessions.GetSourceSalt(), ContextSalt());
		key          = core::hash_string(owner.tag, key);
		key          = core::hash_pod(owner.formatVersion, key);

		std::ranges::sort(moduleEntries);
		for (const auto& [moduleName, entryPoint] : moduleEntries)
		{
			key = core::hash_string(moduleName, key);
			key = core::hash_string(entryPoint, key);
		}
		return key;
	}

	std::filesystem::path
	DiskProgramCache::EntryPath(uint64_t key) const
	{
		return m_Directory / std::format("{:016x}.bsc", key);
	}

	bool
	DiskProgramCache::TryLoadProgram(uint64_t key, std::vector<std::byte>& program) const noexcept
	{
		const std::filesystem::path path = EntryPath(key);

		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
			return false;

		try
		{
			const std::vector<std::byte> file = core::file::read_file_bytes(path);
			core::io::ByteReader         reader(file);

			const auto magic       = reader.ReadPod<uint32_t>();
			const auto storedKey   = reader.ReadPod<uint64_t>();
			const auto payloadHash = reader.ReadPod<uint64_t>();
			auto       payload     = reader.ReadBlob();

			if (magic != c_EntryMagic || storedKey != key || reader.Remaining() != 0 ||
			    core::hash_bytes(payload.data(), payload.size(), core::hash_seed()) != payloadHash)
			{
				spdlog::warn("Ignoring a torn or misplaced shader cache entry {}", path.string());
				return false;
			}

			program = std::move(payload);
			return true;
		}
		catch (const std::exception& e)
		{
			spdlog::warn("Ignoring unreadable shader cache entry {}: {}", path.string(), e.what());
			return false;
		}
	}

	void
	DiskProgramCache::StoreProgram(uint64_t key, std::span<const std::byte> program) const noexcept
	{
		const std::filesystem::path path = EntryPath(key);
		try
		{
			core::io::ByteWriter writer;
			writer.WritePod<uint32_t>(c_EntryMagic);
			writer.WritePod<uint64_t>(key);
			writer.WritePod<uint64_t>(
				core::hash_bytes(program.data(), program.size(), core::hash_seed()));
			writer.WriteBlob(program);
			core::file::write_atomic(path, writer.Take());
		}
		catch (const std::exception& e)
		{
			spdlog::warn("Could not store shader cache entry {}: {}", path.string(), e.what());
		}
	}
}
