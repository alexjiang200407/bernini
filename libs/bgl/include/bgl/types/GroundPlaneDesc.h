#pragma once
#include <bgl/glm.h>

namespace bgl
{
	/**
	 * The ground under a scene, as the plane through `point` with `normal` up: what the skinned
	 * pose pass samples to plant a foot. One plane for the whole scene until a heightfield exists,
	 * and the fallback where none does. The default is `y = 0`, up.
	 */
	struct GroundPlaneDesc
	{
		glm::vec3 point  = glm::vec3(0.0f);
		glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
	};
}
