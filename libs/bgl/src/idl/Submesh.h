// THIS IS A FILE GENERATED FROM Submesh.slang. DO NOT EDIT MANUALLY
#pragma once
#include "Range.h"
#include "RangeWithCount.h"
#include "VertexLayout.h"

namespace bgl::idl
{
	struct Submesh
	{
		VertexLayout layout;
		RangeWithCount meshlets;
		Range vertexMap;
		Range vertexData;
		Range indices;
		uint32_t vertexCount;
		glm::vec4 boundingSphere;
	};

	static_assert(sizeof(Submesh) == 144);
	static_assert(offsetof(Submesh, layout) == 0);
	static_assert(offsetof(Submesh, meshlets) == 104);
	static_assert(offsetof(Submesh, vertexMap) == 112);
	static_assert(offsetof(Submesh, vertexData) == 116);
	static_assert(offsetof(Submesh, indices) == 120);
	static_assert(offsetof(Submesh, vertexCount) == 124);
	static_assert(offsetof(Submesh, boundingSphere) == 128);

}
