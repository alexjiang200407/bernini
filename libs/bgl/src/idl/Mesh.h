// THIS IS A FILE GENERATED FROM Mesh.slang. DO NOT EDIT MANUALLY
#pragma once
#include "RangeWithCount.h"

namespace bgl::idl
{
	struct Mesh
	{
		glm::mat4 transform;
		RangeWithCount submeshes;
		glm::uvec2 pad;
	};

	static_assert(sizeof(Mesh) == 80);
	static_assert(offsetof(Mesh, transform) == 0);
	static_assert(offsetof(Mesh, submeshes) == 64);
	static_assert(offsetof(Mesh, pad) == 72);

}
