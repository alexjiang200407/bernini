#pragma once
#include <assetlib_structs/SourceRef.h>

namespace assetlib
{
	/**
	 * Whether `bytes` opens as a JSON object -- an authored text document rather than a
	 * magic-headed container. Leading whitespace is skipped, exactly as the loaders skip it, so a
	 * tool dispatching on this agrees with what the engine will read.
	 */
	[[nodiscard]] bool
	isTextAssetDocument(std::span<const std::byte> bytes) noexcept;

	/** A cache entry's identity: the token it was written at, and its source. */
	struct CacheEntryInfo
	{
		uint32_t  magic     = 0;
		uint64_t  bakeToken = 0;
		SourceRef source;
	};

	/**
	 * The cache key of any cache-entry container, or nullopt for bytes in another format -- what
	 * `describe --key` prints without loading a payload.
	 */
	[[nodiscard]] std::optional<CacheEntryInfo>
	inspectCacheEntry(std::span<const std::byte> bytes);
}
