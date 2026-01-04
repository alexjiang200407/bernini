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

	Mesh::InfoID
	MeshFactory::CreateCubeInfo(MeshRegistry& registry)
	{
		const auto baseVertex = registry.m_vertices.Size();
		const auto startIndex = registry.m_indices.Size();

		for (const auto& v : cubeVertices) registry.AddVertex(v.position, v.normal, v.uv);

		for (auto idx : cubeIndices) registry.AddIndex(idx);

		const auto indexCount = std::size(cubeIndices);

		return registry.AddInfo(startIndex, indexCount, baseVertex);
	}
}
