#include "mathutil.h"

namespace gfx::math
{
	glm::mat4
	toShaderLayout(const glm::mat4& matrix) noexcept
	{
#ifdef RENDERER_DX11
		glm::mat4 leftHanded = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f)) * matrix;
		return glm::transpose(leftHanded);
#else
		return matrix;
#endif
	}

}
