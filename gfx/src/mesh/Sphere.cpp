#include "math/constants.h"
#include "mesh/MeshFactory.h"
#include "mesh/MeshRegistry.h"

namespace gfx
{
	StaticMeshInfo::ID
	MeshFactory::CreateSphereInfo(MeshRegistry& registry)
	{
		std::vector<Vertex>   sphereVerts;
		std::vector<uint32_t> sphereIndices;

		constexpr uint32_t X_SEGMENTS = 32u;
		constexpr uint32_t Y_SEGMENTS = 32u;
		constexpr float    RADIUS     = 1.0f;

		for (uint32_t y = 0u; y <= Y_SEGMENTS; ++y)
		{
			for (uint32_t x = 0u; x <= X_SEGMENTS; ++x)
			{
				float xSegment = static_cast<float>(x) / static_cast<float>(X_SEGMENTS);
				float ySegment = static_cast<float>(y) / static_cast<float>(Y_SEGMENTS);
				float xPos = std::cos(xSegment * 2.0f * math::PI) * std::sin(ySegment * math::PI);
				float yPos = std::cos(ySegment * math::PI);
				float zPos = std::sin(xSegment * 2.0f * math::PI) * std::sin(ySegment * math::PI);

				Vertex v;
				v.position = glm::vec3(xPos, yPos, zPos) * RADIUS;
				v.normal   = glm::normalize(v.position);
				v.uv       = glm::vec2(xSegment, ySegment);
				sphereVerts.push_back(v);
			}
		}

		for (uint32_t y = 0u; y < Y_SEGMENTS; ++y)
		{
			for (uint32_t x = 0u; x < X_SEGMENTS; ++x)
			{
				sphereIndices.push_back((y + 1u) * (X_SEGMENTS + 1u) + x);
				sphereIndices.push_back(y * (X_SEGMENTS + 1u) + x);
				sphereIndices.push_back(y * (X_SEGMENTS + 1u) + x + 1u);

				sphereIndices.push_back((y + 1u) * (X_SEGMENTS + 1u) + x);
				sphereIndices.push_back(y * (X_SEGMENTS + 1u) + x + 1u);
				sphereIndices.push_back((y + 1u) * (X_SEGMENTS + 1u) + x + 1u);
			}
		}

		uint32_t baseVertexGlobal = registry.AddVertices(sphereVerts);

		std::vector<Meshlet>  generatedMeshlets;
		std::vector<uint32_t> allVertexMapIndices;
		std::vector<uint32_t> allLocalIndices;

		uint32_t totalTriangles     = static_cast<uint32_t>(sphereIndices.size() / 3u);
		uint32_t trianglesProcessed = 0u;

		while (trianglesProcessed < totalTriangles)
		{
			Meshlet                                m{};
			std::vector<uint32_t>                  localVertexMap;
			std::vector<uint32_t>                  localIndices;
			std::unordered_map<uint32_t, uint32_t> localRemap;

			uint32_t localVertexCount   = 0u;
			uint32_t localTriangleCount = 0u;

			while (trianglesProcessed < totalTriangles)
			{
				uint32_t tIdx   = trianglesProcessed * 3u;
				uint32_t tri[3] = { sphereIndices[tIdx],
					                sphereIndices[tIdx + 1u],
					                sphereIndices[tIdx + 2u] };

				uint32_t newVertices = 0u;
				for (uint32_t i = 0; i < 3; ++i)
				{
					if (localRemap.find(tri[i]) == localRemap.end())
						newVertices++;
				}

				if (localVertexCount + newVertices > MAX_VERTICES_PER_MESHLET ||
				    localTriangleCount + 1u > MAX_PRIMS_PER_MESHLET)
					break;

				for (uint32_t i = 0u; i < 3u; ++i)
				{
					uint32_t globalIdx = tri[i];
					if (localRemap.find(globalIdx) == localRemap.end())
					{
						localRemap[globalIdx] = localVertexCount++;
						localVertexMap.push_back(baseVertexGlobal + globalIdx);
					}
					localIndices.push_back(localRemap[globalIdx]);
				}

				localTriangleCount++;
				trianglesProcessed++;
			}

			m.localVertexOffset = static_cast<uint32_t>(allVertexMapIndices.size());
			m.vertexCount       = localVertexCount;
			m.localIndexOffset  = static_cast<uint32_t>(allLocalIndices.size());
			m.triangleCount     = localTriangleCount;

			// Compute bounding sphere
			glm::vec3 minBound(1e10f);
			glm::vec3 maxBound(-1e10f);
			for (auto const& [globalIdx, localIdx] : localRemap)
			{
				minBound = glm::min(minBound, sphereVerts[globalIdx].position);
				maxBound = glm::max(maxBound, sphereVerts[globalIdx].position);
			}
			m.boundingCenter = (minBound + maxBound) * 0.5f;
			m.boundingRadius = glm::distance(maxBound, m.boundingCenter);

			// Accumulate into global arrays
			allVertexMapIndices.insert(
				allVertexMapIndices.end(),
				localVertexMap.begin(),
				localVertexMap.end());
			allLocalIndices.insert(allLocalIndices.end(), localIndices.begin(), localIndices.end());

			generatedMeshlets.push_back(m);
		}

		uint32_t baseMapGlobal     = registry.AddVertexMapIdx(allVertexMapIndices);
		uint32_t baseIndexGlobal   = registry.AddIndices(allLocalIndices);
		uint32_t baseMeshletGlobal = registry.AddMeshlets(generatedMeshlets);

		auto info             = StaticMeshInfo{};
		info.vertexMapSegment = baseMapGlobal;
		info.vertexSegment    = baseVertexGlobal;
		info.indexSegment     = baseIndexGlobal;
		info.meshletSegment   = baseMeshletGlobal;
		info.meshletCount     = static_cast<uint32_t>(generatedMeshlets.size());

		return registry.AddStaticMeshInfo(std::move(info));
	}
}
