// THIS IS A FILE GENERATED FROM Meshlet.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	struct Meshlet
	{
		uint32_t relativeVertexOffset;
		uint32_t relativeIndexOffset;
		uint32_t vertexCount;
		uint32_t triangleCount;
		glm::vec3 boundingCenter;
		float boundingRadius;
	};

	static_assert(sizeof(Meshlet) == 32);
	static_assert(offsetof(Meshlet, relativeVertexOffset) == 0);
	static_assert(offsetof(Meshlet, relativeIndexOffset) == 4);
	static_assert(offsetof(Meshlet, vertexCount) == 8);
	static_assert(offsetof(Meshlet, triangleCount) == 12);
	static_assert(offsetof(Meshlet, boundingCenter) == 16);
	static_assert(offsetof(Meshlet, boundingRadius) == 28);

}
