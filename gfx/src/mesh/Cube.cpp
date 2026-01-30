#include "mesh/MeshFactory.h"
#include "mesh/MeshRegistry.h"
#include "mesh/Vertex.h"

namespace gfx
{
	static const Vertex cubeVertices[] = {
		// 8 unique vertices of a cube
		{ { -1, -1, -1 } },  // 0: left-bottom-back
		{ { 1, -1, -1 } },   // 1: right-bottom-back
		{ { 1, 1, -1 } },    // 2: right-top-back
		{ { -1, 1, -1 } },   // 3: left-top-back
		{ { -1, -1, 1 } },   // 4: left-bottom-front
		{ { 1, -1, 1 } },    // 5: right-bottom-front
		{ { 1, 1, 1 } },     // 6: right-top-front
		{ { -1, 1, 1 } }     // 7: left-top-front
	};

	static const uint32_t cubeIndices[] = { 4, 5, 6, 4, 6, 7, 1, 0, 3, 1, 3, 2, 0, 4, 7, 0, 7, 3,
		                                    5, 1, 2, 5, 2, 6, 7, 6, 2, 7, 2, 3, 0, 1, 5, 0, 5, 4 };

	MeshInfo::ID
	MeshFactory::CreateCubeInfo(MeshRegistry& registry)
	{
		if (auto existingCubeId = registry.GetMeshInfoIDByName("$Cube"))
		{
			return existingCubeId;
		}

		const auto baseVertexGlobal = registry.AddVertices(cubeVertices);

		auto mapIndices = std::vector<uint32_t>(std::size(cubeVertices));
		for (uint32_t i = 0; i < mapIndices.size(); ++i)
		{
			mapIndices[i] = baseVertexGlobal + i;
		}
		const uint32_t baseMapGlobal   = registry.AddVertexMapIdx(mapIndices);
		const auto     baseIndexGlobal = registry.AddIndices(cubeIndices);

		auto m             = Meshlet{};
		m.vertexMapSegment = baseMapGlobal;
		m.vertexCount      = static_cast<uint32_t>(std::size(cubeVertices));
		m.indexSegment     = baseIndexGlobal;
		m.triangleCount    = static_cast<uint32_t>(std::size(cubeIndices)) / 3;
		m.boundingCenter   = glm::vec3{ 0.0f };
		m.boundingRadius   = glm::sqrt(3.0f);

		const auto meshlet           = std::span<const Meshlet>{ &m, 1 };
		const auto baseMeshletGlobal = registry.AddMeshlets(meshlet);

		auto info           = MeshInfo{};
		info.meshletSegment = baseMeshletGlobal;
		info.meshletCount   = 1;
		info.materialID     = 0;

		return registry.AddInfo("$Cube", info, baseVertexGlobal, std::size(cubeVertices));
	}
}
