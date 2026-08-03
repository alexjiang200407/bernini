#pragma once
#include <core/glm.h>

namespace assetlib
{
	/** The null index sentinel shared by the node/mesh/material index fields. */
	inline constexpr uint32_t c_InvalidIndex = 0xFFFFFFFFu;

	struct Transform
	{
		glm::vec3 translation;
		glm::quat rotation;
		glm::vec3 scale;
	};

	static_assert(sizeof(Transform) == 40);

	glm::mat4
	toMatrix(const Transform& transform) noexcept;

	/**
	 * A hierarchy node. Children are encoded as a first-child / next-sibling linked list of indices
	 * into the owning mesh's `nodes`, keeping the node a fixed-size, trivially-copyable POD. A
	 * `uint32_t` index of 0xFFFFFFFF (c_InvalidIndex) is the null sentinel.
	 *
	 * The node is identical between the flattened import form (imp::BMeshImport) and the modular file
	 * form (BMesh); the hierarchy does not change when a mesh is baked to disk, so a single Node type
	 * is shared by both.
	 */
	struct Node
	{
		Transform localTransform;
		uint32_t  parent;       // c_InvalidIndex for roots
		uint32_t  firstChild;   // c_InvalidIndex if leaf
		uint32_t  nextSibling;  // c_InvalidIndex if last child
		uint32_t  mesh;         // index into the owning mesh's `meshes`, or c_InvalidIndex
		uint32_t  nameOffset;   // byte offset into the owning mesh's `stringPool` (0 == empty)
	};

	static_assert(sizeof(Node) == 60);
}
