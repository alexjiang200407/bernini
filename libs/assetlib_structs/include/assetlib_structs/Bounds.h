#pragma once
#include <core/glm.h>

namespace assetlib
{
	/** An axis-aligned box in model space. */
	struct Bounds
	{
		glm::vec3 min;
		glm::vec3 max;
	};
}
