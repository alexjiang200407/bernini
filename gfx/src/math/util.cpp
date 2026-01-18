#include "math/util.h"

namespace gfx
{

	glm::vec3
	toGlm(GfxVec3 v)
	{
		return glm::vec3{ v.x, v.y, v.z };
	}

}
