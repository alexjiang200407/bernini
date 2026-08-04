#include <core/hash.h>

namespace core
{
	namespace
	{
		constexpr uint64_t c_FnvOffset = 14695981039346656037ull;
		constexpr uint64_t c_FnvPrime  = 1099511628211ull;
	}

	uint64_t
	hash_seed() noexcept
	{
		return c_FnvOffset;
	}

	uint64_t
	hash_bytes(const void* data, size_t size, uint64_t seed) noexcept
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
	hash_string(std::string_view str, uint64_t seed) noexcept
	{
		return hash_bytes(str.data(), str.size(), seed);
	}
}
