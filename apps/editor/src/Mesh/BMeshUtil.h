#pragma once

#include <assetlib_structs/BMesh.h>
#include <bgl/glm.h>

namespace bmesh
{
	// A node's transform composed with all of its ancestors'.
	glm::mat4
	GetWorldTransform(const assetlib::BMesh& mesh, uint32_t nodeIndex);

	/**
	 * Where to place an instance of the mesh `nodeIndex` references. GetWorldTransform for ordinary
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
	GetInstanceTransform(const assetlib::BMesh& mesh, uint32_t nodeIndex);

	// Grows [outMin,outMax] to contain the box after `transform`, corner by corner (the box is not
	// axis-aligned once rotated).
	void
	GrowBounds(
		const glm::mat4& transform,
		const glm::vec3& boxMin,
		const glm::vec3& boxMax,
		glm::vec3&       outMin,
		glm::vec3&       outMax);

	// GrowBounds over every submesh box of mesh entry `meshIndex`.
	void
	GrowBoundsForMesh(
		const assetlib::BMesh& mesh,
		uint32_t               meshIndex,
		const glm::mat4&       transform,
		glm::vec3&             outMin,
		glm::vec3&             outMax);

	// Whether `node` references a mesh entry that exists in `mesh` -- not every node carries one.
	[[nodiscard]] bool
	ReferencesMesh(const assetlib::BMesh& mesh, const assetlib::Node& node) noexcept;

	/** One mesh entry a node references, and the world transform an instance of it stands at. */
	struct InstancePlacement
	{
		uint32_t  meshIndex = 0;
		glm::mat4 world     = glm::mat4(1.0f);
	};

	// One entry per node of `mesh` that references a mesh entry, via GetInstanceTransform.
	[[nodiscard]] std::vector<InstancePlacement>
	PlanInstances(const assetlib::BMesh& mesh);

	/**
	 * The mesh entry whose submesh range covers `submeshIndex`, or c_InvalidIndex when none does.
	 *
	 * A submesh is what the editor selects and a mesh is what per-mesh state is authored against,
	 * so anything acting on the second from the first goes through here.
	 */
	[[nodiscard]] uint32_t
	GetMeshOfSubmesh(const assetlib::BMesh& mesh, uint32_t submeshIndex) noexcept;
}
