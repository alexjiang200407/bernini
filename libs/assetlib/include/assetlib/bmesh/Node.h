#pragma once
#include <bgl/glm.h>

namespace assetlib::bmesh
{
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
	 * into `BMesh::nodes`, keeping the node a fixed-size, trivially-copyable POD. A `uint32_t` index
	 * of 0xFFFFFFFF (c_InvalidIndex) is the null sentinel.
	 */
	struct Node
	{
		Transform localTransform;
		uint32_t  parent;       // c_InvalidIndex for roots
		uint32_t  firstChild;   // c_InvalidIndex if leaf
		uint32_t  nextSibling;  // c_InvalidIndex if last child
		uint32_t  mesh;         // index into BMesh::meshes, or c_InvalidIndex
		uint32_t  nameOffset;   // byte offset into BMesh::stringPool (0 == empty)
	};

	static_assert(sizeof(Node) == 60);
}
