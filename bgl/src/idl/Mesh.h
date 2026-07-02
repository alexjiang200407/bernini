// THIS IS A FILE GENERATED FROM Mesh.slang. DO NOT EDIT MANUALLY
#pragma once
#include "RangeWithCount.h"

namespace bgl::idl
{
	struct Mesh
	{
		glm::mat4 transform;
		RangeWithCount submeshes;
		uint32_t totalMeshletCount;
	};

	static_assert(sizeof(Mesh) == 76);
	static_assert(offsetof(Mesh, transform) == 0);
	static_assert(offsetof(Mesh, submeshes) == 64);
	static_assert(offsetof(Mesh, totalMeshletCount) == 72);

}
