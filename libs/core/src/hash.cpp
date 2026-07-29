#include <core/hash.h>

namespace core
{
	namespace
	{
		constexpr uint64_t c_Fnv1aPrime = 1099511628211ull;
	}

	uint64_t
	fnv1a(std::span<const std::byte> bytes, uint64_t seed) noexcept
	{
		uint64_t hash = seed;
		for (const std::byte b : bytes)
		{
			hash ^= static_cast<uint64_t>(b);
			hash *= c_Fnv1aPrime;
		}
		return hash;
	}

	uint64_t
	fnv1a(std::string_view text, uint64_t seed) noexcept
	{
		return fnv1a(std::as_bytes(std::span<const char>(text)), seed);
	}
}
