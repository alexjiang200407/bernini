// THIS IS A FILE GENERATED FROM Meshlet.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	struct Meshlet
	{
		uint32_t relativeVertexOffset;
		uint32_t relativeIndexOffset;
		uint16_t vertexCount;
		uint16_t triangleCount;
		glm::vec3 boundingCenter;
		float boundingRadius;
	};

	static_assert(sizeof(Meshlet) == 28);
	static_assert(offsetof(Meshlet, relativeVertexOffset) == 0);
	static_assert(offsetof(Meshlet, relativeIndexOffset) == 4);
	static_assert(offsetof(Meshlet, vertexCount) == 8);
	static_assert(offsetof(Meshlet, triangleCount) == 10);
	static_assert(offsetof(Meshlet, boundingCenter) == 12);
	static_assert(offsetof(Meshlet, boundingRadius) == 24);

}
