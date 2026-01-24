#pragma once

namespace gfx
{

	static constexpr auto MAX_PRIMS_PER_MESHLET    = 124u;
	static constexpr auto MAX_VERTICES_PER_MESHLET = 64u;

	struct MeshInfo final
	{
		using ID = uint32_t;

		MeshInfo() = default;
		MeshInfo(
			uint32_t baseIndex,
			uint32_t count,
			uint32_t vertexStart,
			uint32_t materialID,
			float    boundingCenter,
			float    boundingRadius) :
			meshletBaseIndex(baseIndex), meshletCount(count), materialID(materialID)
		{}

		uint32_t meshletBaseIndex = 0u;
		uint32_t meshletCount     = 0u;
		uint32_t materialID       = 0u;  // TODO: Material::ID
	};

	struct MeshInstance final
	{
		using ID = uint32_t;

		MeshInstance() = default;
		MeshInstance(MeshInfo::ID id, glm::mat4 modelTransform) :
			infoID(id), modelTransform(modelTransform)
		{}

		MeshInfo::ID infoID = 0u;
		uint32_t     pad[3]{};
		glm::mat4    modelTransform{};
	};

	struct Meshlet final
	{
		using ID = uint32_t;

		Meshlet() = default;
		Meshlet(
			uint32_t  vertexOffset,
			uint32_t  vertexCount,
			uint32_t  indexOffset,
			uint32_t  indexCount,
			glm::vec3 boundingCenter,
			float     boundingRadius) :
			vertexMapOffset(vertexOffset), vertexCount(vertexCount), indexOffset(indexOffset),
			indexCount(indexCount), boundingCenter(boundingCenter), boundingRadius(boundingRadius)
		{}

		uint32_t  vertexMapOffset = 0u;
		uint32_t  vertexCount     = 0u;
		uint32_t  indexOffset     = 0u;
		uint32_t  indexCount      = 0u;
		uint32_t  triangleCount   = 0u;
		glm::vec3 boundingCenter{};
		float     boundingRadius = 0.0f;
	};
}
