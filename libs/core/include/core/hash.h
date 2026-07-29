#pragma once

namespace core
{
	/**
	 * The FNV-1a 64-bit offset basis: the seed a fresh hash starts from, and what to pass to chain
	 * one fnv1a call onto another.
	 */
	inline constexpr uint64_t c_Fnv1aBasis = 14695981039346656037ull;

	/**
	 * FNV-1a over `bytes`. Not cryptographic: use it for content-addressed names and cache keys,
	 * where a collision aliases two entries rather than defeating anything.
	 *
	 * Streaming inputs chain: hash the first chunk with the default seed, each later chunk with the
	 * previous result.
	 */
	[[nodiscard]] uint64_t
	fnv1a(std::span<const std::byte> bytes, uint64_t seed = c_Fnv1aBasis) noexcept;

	[[nodiscard]] uint64_t
	fnv1a(std::string_view text, uint64_t seed = c_Fnv1aBasis) noexcept;
}
