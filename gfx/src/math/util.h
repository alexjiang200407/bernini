#pragma once
#include <gfx/ffi/common.h>

namespace gfx
{
	glm::vec3
	toGlm(GfxVec3 v);

	namespace math_constants
	{
		static inline const glm::vec3 UP_VEC  = glm::vec3{ 0.0f, 1.0f, 0.0f };
		static constexpr float        EPSILON = 1e-6f;
	}

	template <std::integral T>
	constexpr T
	align(T num, T sz) noexcept
	{
		return (num + sz - 1) & ~(sz - 1);
	}

	template <std::integral T>
	constexpr T
	alignNext(T num, T sz) noexcept
	{
		return (num & (sz - 1)) == 0 ? num : (num + sz - (num & (sz - 1)));
	}
}
