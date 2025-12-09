#pragma once
#include <gfx/ffi/common.h>

namespace gfx::math
{
	glm::vec3
	toGlm(GfxVec3 v);

	namespace constants
	{
		static inline const glm::vec3 UP_VEC  = glm::vec3{ 0.0f, 1.0f, 0.0f };
		static constexpr float        EPSILON = 1e-6f;
	}
}
