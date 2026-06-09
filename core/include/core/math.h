#pragma once

namespace core
{
	template <std::integral T, std::integral U>
	[[nodiscard]] constexpr T
	align(T value, U alignment) noexcept
	{
		return (value + static_cast<T>(alignment) - 1) & ~static_cast<T>(alignment - 1);
	}
}
