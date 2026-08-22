#pragma once

namespace assetlib
{
	/**
	 * The engine's bake revision, one token per cache-entry container. A change to what a container
	 * stores -- layout or meaning -- is one edit: replace its token with a fresh random value, and
	 * every file written before the change becomes a cache miss.
	 *
	 * Shared between each container's writer and the regeneration seam, which is the one reader
	 * allowed to see a mismatch as "regenerate" rather than an error.
	 */
	inline constexpr uint64_t c_BMeshBakeToken = 0x6f1d3a58c2e94b07ull;
	inline constexpr uint64_t c_BSkelBakeToken = 0x9be47d02a15c68f3ull;
	inline constexpr uint64_t c_BAnimBakeToken = 0x41f8b6d95e07c2aaull;
}
