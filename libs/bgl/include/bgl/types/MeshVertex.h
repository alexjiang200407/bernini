#pragma once
#include <bgl/glm.h>

namespace bgl
{
	/**
	 * One vertex of geometry built in memory rather than cooked: what IScene::AddTriangleGeom takes,
	 * and what the cube, sphere and plane are built of. 48 bytes, uploaded as they stand.
	 */
	struct MeshVertex
	{
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec4
			tangent;  // xyz along +u, w the handedness that makes cross(normal, xyz) * w run along +v
	};
}
