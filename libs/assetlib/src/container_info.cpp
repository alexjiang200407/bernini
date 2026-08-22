#include "cache_io.h"
#include <assetlib/container_info.h>
#include <assetlib_structs/magic.h>

#include "chunk_io.h"

#include <schema/convert.h>

namespace assetlib
{
	std::optional<CacheEntryInfo>
	inspectCacheEntry(std::span<const std::byte> bytes)
	{
		if (bytes.size() < sizeof(uint32_t))
			return std::nullopt;
		uint32_t magic = 0;
		std::memcpy(&magic, bytes.data(), sizeof(magic));
		if (magic != magic::c_BMesh && magic != magic::c_BSkel && magic != magic::c_BAnim)
			return std::nullopt;

		const cache::PeekedKey key = cache::peekKey(bytes, magic, "cache entry");
		return CacheEntryInfo{ magic, key.bakeToken, key.source };
	}

	bool
	isTextAssetDocument(std::span<const std::byte> bytes) noexcept
	{
		for (const std::byte byte : bytes)
		{
			const char c = static_cast<char>(byte);
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
				continue;
			return c == '{';
		}
		return false;
	}

	ContainerInfo
	inspectContainer(std::span<const std::byte> bytes)
	{
		chunk::Inspection inspection = chunk::inspect(bytes, "container");
		return ContainerInfo{
			inspection.header.magic,
			inspection.header.versionMajor,
			inspection.header.versionMinor,
			std::move(inspection.stored),
		};
	}

	std::string
	describe(const schema::Schema& schema)
	{
		std::string out;
		for (const schema::Layout& layout : schema.GetLayouts())
		{
			out += std::format("  {} ({} bytes)\n", layout.name, layout.size);
			for (const schema::Field& field : layout.fields)
				out += std::format(
					"    {:<24} {:<28} @{}\n",
					field.name,
					schema::fieldShape(schema, field),
					field.offset);
		}
		return out;
	}
}
