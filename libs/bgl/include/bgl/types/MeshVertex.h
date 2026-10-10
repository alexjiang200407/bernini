#pragma once
#include <bgl/glm.h>
#include <cstddef>

namespace bgl
{
	/**
	 * One vertex of geometry built in memory rather than cooked: what IScene::AddTriangleGeom takes,
	 * and what the cube, sphere and plane are built of. 48 bytes, uploaded as they stand. `tangent`
	 * runs along +u, its w the handedness that makes cross(normal, tangent.xyz) * w run along +v.
	 */
	struct MeshVertex
	{
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec4 tangent;
	};

	static_assert(sizeof(MeshVertex) == 48, "a MeshVertex is uploaded as it stands");
	static_assert(
		offsetof(MeshVertex, normal) == 12 && offsetof(MeshVertex, uv) == 24 &&
			offsetof(MeshVertex, tangent) == 32,
		"a MeshVertex is laid out as the procedural VertexLayout reads it");
}
