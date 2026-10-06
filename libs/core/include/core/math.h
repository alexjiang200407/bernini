#pragma once
#include <cmath>
#include <concepts>
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

	/** Whether `value` is finite and above zero: a length, a size or a rate a caller may pass. */
	template <std::floating_point T>
	[[nodiscard]] bool
	is_finite_positive(const T value) noexcept
	{
		return std::isfinite(value) && value > T(0);
	}

	/** Whether `value` is finite and not below zero. */
	template <std::floating_point T>
	[[nodiscard]] bool
	is_finite_non_negative(const T value) noexcept
	{
		return std::isfinite(value) && value >= T(0);
	}

	/** Whether `value` lies in [0, 1], which no NaN does: a share, a weight, a blend. */
	template <std::floating_point T>
	[[nodiscard]] bool
	is_unit_interval(const T value) noexcept
	{
		return value >= T(0) && value <= T(1);
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

	/** (center, radius) circumscribing the box, so it is conservative for whatever the box held. */
	[[nodiscard]] inline glm::vec4
	bounding_sphere_of(const glm::vec3& minBound, const glm::vec3& maxBound) noexcept
	{
		const glm::vec3 center = (minBound + maxBound) * 0.5f;
		return glm::vec4(center, glm::distance(maxBound, center));
	}
}
