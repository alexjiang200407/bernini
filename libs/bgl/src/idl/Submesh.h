// THIS IS A FILE GENERATED FROM Submesh.slang. DO NOT EDIT MANUALLY
#pragma once
#include "Entry.h"
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
		Entry material;
		uint32_t pso;
	};

	static_assert(sizeof(Submesh) == 132);
	static_assert(offsetof(Submesh, layout) == 0);
	static_assert(offsetof(Submesh, meshlets) == 100);
	static_assert(offsetof(Submesh, vertexMap) == 108);
	static_assert(offsetof(Submesh, vertexData) == 112);
	static_assert(offsetof(Submesh, indices) == 116);
	static_assert(offsetof(Submesh, vertexCount) == 120);
	static_assert(offsetof(Submesh, material) == 124);
	static_assert(offsetof(Submesh, pso) == 128);

}
