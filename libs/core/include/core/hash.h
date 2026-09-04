#pragma once
#include <concepts>
#include <core/type_traits.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace core
{
	// FNV-1a over raw bytes. Seeded so calls chain: the result of one is the seed of the next.
	// Not cryptographic, and not stable across a change to this function -- anything persisted
	// under it must key its own format version in.
	[[nodiscard]] uint64_t
	hash_bytes(const void* data, size_t size, uint64_t seed) noexcept;

	[[nodiscard]] uint64_t
	hash_string(std::string_view str, uint64_t seed) noexcept;

	// The seed to start a chain from.
	[[nodiscard]] uint64_t
	hash_seed() noexcept;

	template <type_traits::trivially_copyable T>
		requires(!std::convertible_to<const T&, std::span<const std::byte>>)
	[[nodiscard]] uint64_t
	hash_pod(const T& value, uint64_t seed) noexcept
	{
		return hash_bytes(&value, sizeof(T), seed);
	}
}
