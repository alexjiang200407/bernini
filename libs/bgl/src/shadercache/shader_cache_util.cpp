#include "shadercache/shader_cache_util.h"

#include <core/file/file.h>

#if defined(_WIN32)
#	include <process.h>
#else
#	include <unistd.h>
#endif

namespace bgl::shader_cache
{
	namespace
	{
		constexpr uint64_t c_FnvOffset = 14695981039346656037ull;
		constexpr uint64_t c_FnvPrime  = 1099511628211ull;

	}

	uint32_t
	ProcessId() noexcept
	{
#if defined(_WIN32)
		return static_cast<uint32_t>(_getpid());
#else
		return static_cast<uint32_t>(::getpid());
#endif
	}

	uint64_t
	HashBytes(const void* data, size_t size, uint64_t seed)
	{
		const auto* bytes = static_cast<const uint8_t*>(data);
		uint64_t    hash  = seed;
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= bytes[i];
			hash *= c_FnvPrime;
		}
		return hash;
	}

	uint64_t
	HashString(std::string_view str, uint64_t seed)
	{
		return HashBytes(str.data(), str.size(), seed);
	}

	uint64_t
	ComputeSourceSalt(
		std::string_view                optionsSalt,
		const std::vector<std::string>& searchPaths,
		uint32_t                        formatVersion)
	{
		namespace fs = std::filesystem;

		uint64_t salt = HashString(optionsSalt, c_FnvOffset);
		salt          = HashBytes(&formatVersion, sizeof(formatVersion), salt);

		// Sort by path so the salt is order-independent across filesystem walks.
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

		std::sort(files.begin(), files.end());

		for (const fs::path& file : files)
		{
			const std::string path = file.generic_string();
			salt                   = HashString(path, salt);

			const std::vector<std::byte> bytes = core::file::read_file_bytes(path);
			salt                               = HashBytes(bytes.data(), bytes.size(), salt);
		}

		return salt;
	}

	uint64_t
	ComputeKey(uint64_t salt, std::vector<std::pair<std::string, std::string>> moduleEntries)
	{
		std::sort(moduleEntries.begin(), moduleEntries.end());

		uint64_t key = salt;
		for (const auto& [module, entry] : moduleEntries)
		{
			key = HashString(module, key);
			key = HashString(entry, key);
		}
		return key;
	}

	std::filesystem::path
	KeyPath(const std::filesystem::path& dir, uint64_t key)
	{
		return dir / std::format("{:016x}.bsc", key);
	}

	bool
	WriteFileAtomic(const std::filesystem::path& path, std::span<const std::byte> bytes)
	{
		static std::atomic<uint32_t> counter = 0;

		const std::filesystem::path tmp = std::format(
			"{}.{}.{}.tmp",
			path.string(),
			ProcessId(),
			counter.fetch_add(1, std::memory_order_relaxed));

		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			logger::warn("Could not open cache file for writing: {}", tmp.string());
			return false;
		}

		out.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		out.close();

		std::error_code ec;
		std::filesystem::rename(tmp, path, ec);
		if (ec)
		{
			logger::warn("Could not commit cache file {}: {}", path.string(), ec.message());
			std::filesystem::remove(tmp, ec);
			return false;
		}
		return true;
	}

	void
	WriteString(core::io::ByteWriter& writer, std::string_view value)
	{
		writer.writePod<uint32_t>(static_cast<uint32_t>(value.size()));
		writer.writeBytes(std::as_bytes(std::span<const char>(value.data(), value.size())));
	}

	std::string
	ReadString(core::io::ByteReader& reader)
	{
		const uint32_t                   size = reader.readPod<uint32_t>();
		const std::span<const std::byte> raw  = reader.readBytes(size);
		return std::string(reinterpret_cast<const char*>(raw.data()), size);
	}

	void
	WriteBlob(core::io::ByteWriter& writer, std::span<const std::byte> value)
	{
		writer.writePod<uint32_t>(static_cast<uint32_t>(value.size()));
		writer.writeBytes(value);
	}

	std::vector<std::byte>
	ReadBlob(core::io::ByteReader& reader)
	{
		const uint32_t                   size = reader.readPod<uint32_t>();
		const std::span<const std::byte> raw  = reader.readBytes(size);
		return std::vector<std::byte>(raw.begin(), raw.end());
	}

	void
	WriteLayout(core::io::ByteWriter& writer, const ReflectedLayout& layout)
	{
		writer.writePod<uint32_t>(static_cast<uint32_t>(layout.kind));
		writer.writePod<uint32_t>(static_cast<uint32_t>(layout.valueType));
		writer.writePod<uint32_t>(layout.size);
		writer.writePod<uint32_t>(layout.arrayCount);
		writer.writePod<uint32_t>(layout.arrayStride);

		writer.writePod<uint32_t>(static_cast<uint32_t>(layout.fields.size()));
		for (const ReflectedField& field : layout.fields)
		{
			WriteString(writer, field.name);
			writer.writePod<uint32_t>(field.offset);
			WriteLayout(writer, field.layout);
		}

		writer.writePod<uint32_t>(static_cast<uint32_t>(layout.element.size()));
		for (const ReflectedLayout& element : layout.element) WriteLayout(writer, element);
	}

	ReflectedLayout
	ReadLayout(core::io::ByteReader& reader)
	{
		ReflectedLayout layout;
		layout.kind        = static_cast<UniformType>(reader.readPod<uint32_t>());
		layout.valueType   = static_cast<UniformValueType>(reader.readPod<uint32_t>());
		layout.size        = reader.readPod<uint32_t>();
		layout.arrayCount  = reader.readPod<uint32_t>();
		layout.arrayStride = reader.readPod<uint32_t>();

		const uint32_t fieldCount = reader.readPod<uint32_t>();
		layout.fields.reserve(fieldCount);
		for (uint32_t i = 0; i < fieldCount; ++i)
		{
			ReflectedField field;
			field.name   = ReadString(reader);
			field.offset = reader.readPod<uint32_t>();
			field.layout = ReadLayout(reader);
			layout.fields.push_back(std::move(field));
		}

		const uint32_t elementCount = reader.readPod<uint32_t>();
		layout.element.reserve(elementCount);
		for (uint32_t i = 0; i < elementCount; ++i) layout.element.push_back(ReadLayout(reader));

		return layout;
	}
}
