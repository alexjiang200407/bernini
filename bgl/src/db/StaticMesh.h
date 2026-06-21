#pragma once
#include "db/Entry.h"
#include "db/Meshlet.h"
#include "db/Range.h"
#include "db/Vertex.h"

namespace bgl::db
{
	struct StaticGeom
	{
		Range          vertexMap;
		Range          vertices;
		Range          indices;
		RangeWithCount meshlets;
	};

	struct StaticMeshInstance
	{
		Entry     base;
		glm::mat4 transform;
	};
}
