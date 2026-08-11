#pragma once

#include <assetlib_structs/BMesh.h>
#include <bgl/glm.h>

namespace bmesh
{
	// A node's transform composed with all of its ancestors'.
	glm::mat4
	WorldTransform(const assetlib::BMesh& mesh, uint32_t nodeIndex);

	/**
	 * Where to place an instance of the mesh `nodeIndex` references. WorldTransform for ordinary
	 * geometry; identity when that mesh is skinned.
	 *
	 * A skinned mesh's vertices are in the skin's space, and glTF 3.7.4 has the node's own transform
	 * ignored for exactly that reason -- the joints place them, and the joint chain already carries
	 * whatever the armature contributes. Composing the hierarchy on top applies the armature twice,
	 * which is a rotation and a scale the mesh has already had.
	 *
	 * This is the rule for both bind pose and animation: a pose evaluates into the same space.
	 */
	glm::mat4
	InstanceTransform(const assetlib::BMesh& mesh, uint32_t nodeIndex);

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
