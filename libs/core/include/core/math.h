#pragma once
#include <core/glm.h>

namespace core
{
	// Long enough for a double; a float initialiser just truncates it.
	inline constexpr double c_Pi = 3.14159265358979323846;

	/**
	 * Whether every component of `v` is finite -- neither infinite nor NaN.
	 *
	 * What a setter taking a position or a direction checks before storing one: neither normalising
	 * nor comparing a vector that fails this yields anything a later reader can use, and the
	 * failure surfaces wherever that vector is next divided by rather than where it came in.
	 */
	template <glm::length_t L, std::floating_point T, glm::qualifier Q>
	[[nodiscard]] bool
	is_finite(const glm::vec<L, T, Q>& v) noexcept
	{
		for (glm::length_t i = 0; i < L; ++i)
		{
			if (!std::isfinite(v[i]))
			{
				return false;
			}
		}
		return true;
	}

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
