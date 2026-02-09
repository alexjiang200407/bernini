#pragma once

namespace gfx
{

	static constexpr auto MAX_PRIMS_PER_MESHLET    = 124u;
	static constexpr auto MAX_VERTICES_PER_MESHLET = 64u;

	struct StaticMeshInfo final
	{
		using ID = uint32_t;

		uint32_t vertexMapSegment = 0u;
		uint32_t vertexSegment    = 0u;
		uint32_t indexSegment     = 0u;
		uint32_t meshletSegment   = 0u;
		uint32_t meshletCount     = 0u;
	};

	struct StaticMeshInstance final
	{
		using ID = uint32_t;

		StaticMeshInfo::ID infoID = 0u;
		uint32_t           pad[3]{};
		glm::mat4          modelTransform{};
	};

	struct Meshlet final
	{
		using ID = uint32_t;

		uint32_t localVertexOffset = 0u;
		uint32_t vertexCount       = 0u;

		uint32_t localIndexOffset = 0u;
		uint32_t triangleCount    = 0u;

		glm::vec3 boundingCenter{};
		float     boundingRadius = 0.0f;
	};

	struct MeshletIndirectDrawArg final
	{
		uint32_t threadGroupCountX;
		uint32_t threadGroupCountY;
		uint32_t threadGroupCountZ;
		uint32_t visibleBufferOffset;
	};

}
