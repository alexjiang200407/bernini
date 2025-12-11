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
}
