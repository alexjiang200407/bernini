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

	static const uint16_t cubeIndices[] = { 4, 5, 6, 4, 6, 7, 1, 0, 3, 1, 3, 2, 0, 4, 7, 0, 7, 3,
		                                    5, 1, 2, 5, 2, 6, 7, 6, 2, 7, 2, 3, 0, 1, 5, 0, 5, 4 };

	MeshInfo::ID
	MeshFactory::CreateCubeInfo(MeshRegistry& registry)
	{
		const auto baseVertexGlobal  = static_cast<uint32_t>(registry.m_vertices.Size());
		const auto baseIndexGlobal   = static_cast<uint32_t>(registry.m_indices.Size());
		const auto baseMapGlobal     = static_cast<uint32_t>(registry.m_vertexMap.Size());
		const auto baseMeshletGlobal = static_cast<uint32_t>(registry.m_meshlets.Size());

		for (const auto& v : cubeVertices)
		{
			registry.AddVertex(Vertex{ v.position, v.normal, v.uv });
		}

		for (uint32_t i = 0; i < std::size(cubeVertices); ++i)
		{
			registry.AddVertexMapIdx(baseVertexGlobal + i);
		}

		for (auto idx : cubeIndices)
		{
			registry.AddIndex(static_cast<uint32_t>(idx));
		}

		auto m            = Meshlet{};
		m.vertexMapOffset = baseMapGlobal;
		m.vertexCount     = static_cast<uint32_t>(std::size(cubeVertices));
		m.indexOffset     = baseIndexGlobal;
		m.triangleCount   = static_cast<uint32_t>(std::size(cubeIndices)) / 3;
		m.boundingCenter  = glm::vec3{ 0.0f };
		m.boundingRadius  = glm::sqrt(3.0f);

		registry.AddMeshlet(std::move(m));

		auto info             = MeshInfo{};
		info.meshletBaseIndex = baseMeshletGlobal;
		info.meshletCount     = 1;
		info.materialID       = 0;

		return registry.AddInfo(std::move(info));
	}
}
