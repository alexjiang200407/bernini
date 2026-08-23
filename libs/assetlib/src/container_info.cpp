#include <assetlib/container_info.h>

#include "cache_io.h"

namespace assetlib
{
	std::optional<CacheEntryInfo>
	inspectCacheEntry(std::span<const std::byte> bytes)
	{
		if (!cache::isCacheEntry(bytes))
			return std::nullopt;
		uint32_t magic = 0;
		std::memcpy(&magic, bytes.data(), sizeof(magic));

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
}
