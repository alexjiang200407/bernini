#pragma once

namespace core
{
	template <std::integral T, std::integral U>
	[[nodiscard]] constexpr T
	align(T value, U alignment) noexcept
	{
		return (value + static_cast<T>(alignment) - 1) & ~static_cast<T>(alignment - 1);
	}

	/**
	 * Ceiling division: the number of `divisor`-sized buckets needed to cover `value`, i.e.
	 * ceil(value / divisor). For a whole-thread-group dispatch this is the group count.
	 */
	template <std::integral T, std::integral U>
	[[nodiscard]] constexpr T
	div_ceil(T value, U divisor) noexcept
	{
		return (value + static_cast<T>(divisor) - 1) / static_cast<T>(divisor);
	}

	/**
	 * `value` rounded up to the next multiple of `multiple` -- the padded extent that covers a
	 * whole number of groups. Unlike align() this works for any multiple, not just powers of two.
	 */
	template <std::integral T, std::integral U>
	[[nodiscard]] constexpr T
	round_up(T value, U multiple) noexcept
	{
		return div_ceil(value, multiple) * static_cast<T>(multiple);
	}
}
