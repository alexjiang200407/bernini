#pragma once

namespace bgl::db
{
	struct Meshlet
	{
		uint32_t relativeVertexOffset;  // Relative to the parent Mesh vertexSegment
		uint32_t relativeIndexOffset;   // Relative to the parent Mesh indexSegment

		uint16_t vertexCount;
		uint16_t triangleCount;

		glm::vec3 boundingCenter;
		float     boundingRadius;
	};
}
