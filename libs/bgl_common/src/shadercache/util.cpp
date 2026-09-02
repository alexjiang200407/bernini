#include <bgl_common/shadercache/util.h>

#include <bgl_common/gassert.h>

#include <core/file/file.h>
#include <core/hash.h>
#include <core/platform/util.h>

namespace bgl::shader_cache
{
	uint64_t
	ComputeSourceSalt(
		std::string_view                optionsSalt,
		const std::vector<std::string>& searchPaths,
		uint32_t                        formatVersion)
	{
		namespace fs = std::filesystem;

		uint64_t salt = core::hash_string(optionsSalt, core::hash_seed());
		salt          = core::hash_pod(formatVersion, salt);

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
			salt                   = core::hash_string(path, salt);

			const std::vector<std::byte> bytes = core::file::read_file_bytes(path);
			salt                               = core::hash_bytes(bytes.data(), bytes.size(), salt);
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
			key = core::hash_string(module, key);
			key = core::hash_string(entry, key);
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
		static std::atomic<uint32_t> g_Counter = 0;

		const std::filesystem::path tmp = std::format(
			"{}.{}.{}.tmp",
			path.string(),
			core::process_id(),
			g_Counter.fetch_add(1, std::memory_order_relaxed));

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
		writer.WritePod<uint32_t>(static_cast<uint32_t>(value.size()));
		writer.WriteBytes(std::as_bytes(std::span<const char>(value.data(), value.size())));
	}

	std::string
	ReadString(core::io::ByteReader& reader)
	{
		const uint32_t                   size = reader.ReadPod<uint32_t>();
		const std::span<const std::byte> raw  = reader.ReadBytes(size);
		return std::string(reinterpret_cast<const char*>(raw.data()), size);
	}

	void
	WriteBlob(core::io::ByteWriter& writer, std::span<const std::byte> value)
	{
		writer.WritePod<uint32_t>(static_cast<uint32_t>(value.size()));
		writer.WriteBytes(value);
	}

	std::vector<std::byte>
	ReadBlob(core::io::ByteReader& reader)
	{
		const uint32_t                   size = reader.ReadPod<uint32_t>();
		const std::span<const std::byte> raw  = reader.ReadBytes(size);
		return std::vector<std::byte>(raw.begin(), raw.end());
	}

	void
	WriteLayout(core::io::ByteWriter& writer, const ReflectedLayout& layout)
	{
		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.kind));
		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.valueType));
		writer.WritePod<uint32_t>(layout.size);
		writer.WritePod<uint32_t>(layout.arrayCount);
		writer.WritePod<uint32_t>(layout.arrayStride);

		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.fields.size()));
		for (const ReflectedField& field : layout.fields)
		{
			WriteString(writer, field.name);
			writer.WritePod<uint32_t>(field.offset);
			WriteLayout(writer, field.layout);
		}

		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.element.size()));
		for (const ReflectedLayout& element : layout.element) WriteLayout(writer, element);
	}

	ReflectedLayout
	ReadLayout(core::io::ByteReader& reader)
	{
		ReflectedLayout layout;
		layout.kind        = static_cast<UniformType>(reader.ReadPod<uint32_t>());
		layout.valueType   = static_cast<UniformValueType>(reader.ReadPod<uint32_t>());
		layout.size        = reader.ReadPod<uint32_t>();
		layout.arrayCount  = reader.ReadPod<uint32_t>();
		layout.arrayStride = reader.ReadPod<uint32_t>();

		const uint32_t fieldCount = reader.ReadPod<uint32_t>();
		layout.fields.reserve(fieldCount);
		for (uint32_t i = 0; i < fieldCount; ++i)
		{
			ReflectedField field;
			field.name   = ReadString(reader);
			field.offset = reader.ReadPod<uint32_t>();
			field.layout = ReadLayout(reader);
			layout.fields.push_back(std::move(field));
		}

		const uint32_t elementCount = reader.ReadPod<uint32_t>();
		layout.element.reserve(elementCount);
		for (uint32_t i = 0; i < elementCount; ++i) layout.element.push_back(ReadLayout(reader));

		return layout;
	}
}
