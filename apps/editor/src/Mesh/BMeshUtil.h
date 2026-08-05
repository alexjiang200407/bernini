#pragma once

#include <assetlib_structs/BMesh.h>
#include <bgl/glm.h>

namespace bmesh
{
	// A node's transform composed with all of its ancestors'.
	glm::mat4
	WorldTransform(const assetlib::BMesh& mesh, uint32_t nodeIndex);

	// Grows [outMin,outMax] to contain the box after `transform`, corner by corner (the box is not
	// axis-aligned once rotated).
	void
	GrowBounds(
		const glm::mat4& transform,
		const glm::vec3& boxMin,
		const glm::vec3& boxMax,
		glm::vec3&       outMin,
		glm::vec3&       outMax);
}
