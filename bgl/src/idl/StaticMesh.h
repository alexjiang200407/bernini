// THIS IS A FILE GENERATED FROM StaticMesh.slang. DO NOT EDIT MANUALLY
#pragma once
#include "Entry.h"
#include "Range.h"

namespace bgl::idl
{
	struct StaticGeom
	{
		Range vertexMap;
		Range vertices;
		Range indices;
		RangeWithCount meshlets;
	};

	static_assert(sizeof(StaticGeom) == 20);
	static_assert(offsetof(StaticGeom, vertexMap) == 0);
	static_assert(offsetof(StaticGeom, vertices) == 4);
	static_assert(offsetof(StaticGeom, indices) == 8);
	static_assert(offsetof(StaticGeom, meshlets) == 12);

	struct StaticMeshInstance
	{
		Entry base;
		glm::mat4 transform;
	};

	static_assert(sizeof(StaticMeshInstance) == 68);
	static_assert(offsetof(StaticMeshInstance, base) == 0);
	static_assert(offsetof(StaticMeshInstance, transform) == 4);

}
