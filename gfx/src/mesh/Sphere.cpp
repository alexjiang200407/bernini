#include "math/constants.h"
#include "mesh/MeshFactory.h"
#include "mesh/MeshRegistry.h"

namespace
{
	struct SphereVertex
	{
		glm::vec3 pos;
		glm::vec3 normal;
	};

	constexpr uint32_t kStacks = 16;
	constexpr uint32_t kSlices = 32;

	void
	BuildSphere(std::vector<SphereVertex>& vertices, std::vector<uint32_t>& indices)
	{
		vertices.clear();
		indices.clear();

		const float radius = 1.0f;

		for (uint32_t stack = 0; stack <= kStacks; ++stack)
		{
			float v   = float(stack) / float(kStacks);
			float phi = v * glm::pi<float>();

			float y = std::cos(phi);
			float r = std::sin(phi);

			for (uint32_t slice = 0; slice <= kSlices; ++slice)
			{
				float u     = float(slice) / float(kSlices);
				float theta = u * glm::two_pi<float>();

				float x = r * std::cos(theta);
				float z = r * std::sin(theta);

				glm::vec3 normal = glm::normalize(glm::vec3(x, y, z));

				vertices.push_back({ normal * radius, normal });
			}
		}

		const uint32_t stride = kSlices + 1;

		for (uint32_t stack = 0; stack < kStacks; ++stack)
		{
			for (uint32_t slice = 0; slice < kSlices; ++slice)
			{
				auto i0 = uint32_t(stack * stride + slice);
				auto i1 = uint32_t(i0 + stride);
				auto i2 = uint32_t(i0 + 1);
				auto i3 = uint32_t(i1 + 1);

				indices.push_back(i0);
				indices.push_back(i1);
				indices.push_back(i2);

				indices.push_back(i2);
				indices.push_back(i1);
				indices.push_back(i3);
			}
		}
	}
}

namespace gfx
{
	MeshInfo::ID
	MeshFactory::CreateSphereInfo(MeshRegistry& registry)
	{
		//if (auto existingSphereId = registry.GetMeshInfoIDByName("$Sphere"))
		//{
		//	return existingSphereId;
		//}

		//std::vector<Vertex>   sphereVerts;
		//std::vector<uint32_t> sphereIndices;

		//constexpr uint32_t X_SEGMENTS = 32u;
		//constexpr uint32_t Y_SEGMENTS = 32u;
		//constexpr float    RADIUS     = 1.0f;

		//for (uint32_t y = 0u; y <= Y_SEGMENTS; ++y)
		//{
		//	for (uint32_t x = 0u; x <= X_SEGMENTS; ++x)
		//	{
		//		float xSegment = (float)x / (float)X_SEGMENTS;
		//		float ySegment = (float)y / (float)Y_SEGMENTS;
		//		float xPos = std::cos(xSegment * 2.0f * math::PI) * std::sin(ySegment * math::PI);
		//		float yPos = std::cos(ySegment * math::PI);
		//		float zPos = std::sin(xSegment * 2.0f * math::PI) * std::sin(ySegment * math::PI);

		//		Vertex v;
		//		v.position = glm::vec3(xPos, yPos, zPos) * RADIUS;
		//		v.normal   = glm::normalize(v.position);
		//		v.uv       = glm::vec2(xSegment, ySegment);
		//		sphereVerts.push_back(v);
		//	}
		//}

		//for (uint32_t y = 0u; y < Y_SEGMENTS; ++y)
		//{
		//	for (uint32_t x = 0u; x < X_SEGMENTS; ++x)
		//	{
		//		sphereIndices.push_back((y + 1u) * (X_SEGMENTS + 1u) + x);
		//		sphereIndices.push_back(y * (X_SEGMENTS + 1u) + x);
		//		sphereIndices.push_back(y * (X_SEGMENTS + 1u) + x + 1u);

		//		sphereIndices.push_back((y + 1u) * (X_SEGMENTS + 1u) + x);
		//		sphereIndices.push_back(y * (X_SEGMENTS + 1u) + x + 1u);
		//		sphereIndices.push_back((y + 1u) * (X_SEGMENTS + 1u) + x + 1u);
		//	}
		//}

		//uint32_t baseVertexGlobal = registry.AddVertices(sphereVerts);

		//std::vector<Meshlet> generatedMeshlets;
		//uint32_t             totalTriangles     = static_cast<uint32_t>(sphereIndices.size() / 3u);
		//uint32_t             trianglesProcessed = 0u;

		//while (trianglesProcessed < totalTriangles)
		//{
		//	Meshlet                                m{};
		//	std::vector<uint32_t>                  localVertexMap;
		//	std::vector<uint32_t>                  localIndices;
		//	std::unordered_map<uint32_t, uint32_t> localRemap;

		//	uint32_t localVertexCount   = 0u;
		//	uint32_t localTriangleCount = 0u;

		//	while (trianglesProcessed < totalTriangles)
		//	{
		//		uint32_t tIdx   = trianglesProcessed * 3u;
		//		uint32_t tri[3] = { sphereIndices[tIdx],
		//			                sphereIndices[tIdx + 1u],
		//			                sphereIndices[tIdx + 2u] };

		//		uint32_t newVertices = 0u;
		//		for (uint32_t i = 0; i < 3; ++i)
		//		{
		//			if (localRemap.find(tri[i]) == localRemap.end())
		//				newVertices++;
		//		}

		//		if (localVertexCount + newVertices > 64u || localTriangleCount + 1u > 124u)
		//			break;

		//		for (uint32_t i = 0; i < 3u; ++i)
		//		{
		//			uint32_t globalIdx = tri[i];
		//			if (localRemap.find(globalIdx) == localRemap.end())
		//			{
		//				localRemap[globalIdx] = localVertexCount++;
		//				localVertexMap.push_back(baseVertexGlobal + globalIdx);
		//			}
		//			localIndices.push_back(localRemap[globalIdx]);
		//		}

		//		localTriangleCount++;
		//		trianglesProcessed++;
		//	}

		//	m.vertexMapSegment = registry.AddVertexMapIdx(localVertexMap);
		//	m.indexSegment     = registry.AddIndices(localIndices);
		//	m.vertexCount      = localVertexCount;
		//	m.triangleCount    = localTriangleCount;

		//	glm::vec3 minBound(1e10f);
		//	glm::vec3 maxBound(-1e10f);
		//	for (auto const& [globalIdx, localIdx] : localRemap)
		//	{
		//		minBound = glm::min(minBound, sphereVerts[globalIdx].position);
		//		maxBound = glm::max(maxBound, sphereVerts[globalIdx].position);
		//	}
		//	m.boundingCenter = (minBound + maxBound) * 0.5f;
		//	m.boundingRadius = glm::distance(maxBound, m.boundingCenter);

		//	generatedMeshlets.push_back(m);
		//}

		//uint32_t baseMeshletGlobal = registry.AddMeshlets(generatedMeshlets);

		//auto info           = MeshInfo{};
		//info.meshletSegment = baseMeshletGlobal;
		//info.meshletCount   = static_cast<uint32_t>(generatedMeshlets.size());
		//info.materialID     = 0;

		//return registry.AddInfo("$Sphere", info, baseVertexGlobal, sphereVerts.size());
		return 0;
	}
}
