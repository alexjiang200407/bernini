#pragma once
#include <core/glm.h>

namespace bgl
{
	/**
	 * One vertex of a procedurally generated primitive (cube, sphere, plane).
	 */
	struct VertexGen
	{
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec4 tangent;
	};
}
