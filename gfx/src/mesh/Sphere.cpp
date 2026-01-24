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
		auto sphereVerts   = std::vector<Vertex>{};
		auto sphereIndices = std::vector<uint32_t>{};

		constexpr auto X_SEGMENTS = 32u;
		constexpr auto Y_SEGMENTS = 32u;
		constexpr auto RADIUS     = 1.0f;

		for (auto y = 0u; y <= Y_SEGMENTS; ++y)
		{
			for (auto x = 0u; x <= X_SEGMENTS; ++x)
			{
				auto xSegment = (float)x / (float)X_SEGMENTS;
				auto ySegment = (float)y / (float)Y_SEGMENTS;
				auto xPos = std::cos(xSegment * 2.0f * math::PI) * std::sin(ySegment * math::PI);
				auto yPos = std::cos(ySegment * math::PI);
				auto zPos = std::sin(xSegment * 2.0f * math::PI) * std::sin(ySegment * math::PI);

				Vertex v;
				v.position = glm::vec3(xPos, yPos, zPos) * RADIUS;
				v.normal   = glm::normalize(v.position);
				v.uv       = glm::vec2(xSegment, ySegment);
				sphereVerts.push_back(v);
			}
		}

		for (auto y = 0u; y < Y_SEGMENTS; ++y)
		{
			for (auto x = 0u; x < X_SEGMENTS; ++x)
			{
				sphereIndices.push_back((y + 1u) * (X_SEGMENTS + 1u) + x);
				sphereIndices.push_back(y * (X_SEGMENTS + 1u) + x);
				sphereIndices.push_back(y * (X_SEGMENTS + 1u) + x + 1u);

				sphereIndices.push_back((y + 1u) * (X_SEGMENTS + 1u) + x);
				sphereIndices.push_back(y * (X_SEGMENTS + 1u) + x + 1u);
				sphereIndices.push_back((y + 1u) * (X_SEGMENTS + 1u) + x + 1u);
			}
		}

		auto baseVertexGlobal  = static_cast<uint32_t>(registry.m_vertices.Size());
		auto baseMeshletGlobal = static_cast<uint32_t>(registry.m_meshlets.Size());

		for (auto const& v : sphereVerts)
		{
			registry.AddVertex(v);
		}

		auto totalTriangles     = static_cast<uint32_t>(sphereIndices.size() / 3u);
		auto trianglesProcessed = 0u;
		while (trianglesProcessed < totalTriangles)
		{
			auto m            = Meshlet{};
			m.vertexMapOffset = static_cast<uint32_t>(registry.m_vertexMap.Size());
			m.indexOffset     = static_cast<uint32_t>(registry.m_indices.Size());

			auto localRemap         = std::unordered_map<uint32_t, uint32_t>{};
			auto localVertexCount   = 0u;
			auto localTriangleCount = 0u;

			while (trianglesProcessed < totalTriangles)
			{
				auto tIdx = trianglesProcessed * 3u;
				auto i0   = sphereIndices[tIdx];
				auto i1   = sphereIndices[tIdx + 1u];
				auto i2   = sphereIndices[tIdx + 2u];

				auto newVertices = 0u;
				if (localRemap.find(i0) == localRemap.end())
					newVertices++;
				if (localRemap.find(i1) == localRemap.end())
					newVertices++;
				if (localRemap.find(i2) == localRemap.end())
					newVertices++;

				if (localVertexCount + newVertices > 64u || localTriangleCount + 1u > 124u)
				{
					break;
				}

				auto indices = std::array<uint32_t, 3>{ i0, i1, i2 };
				for (auto i = 0u; i < 3u; ++i)
				{
					if (localRemap.find(indices[i]) == localRemap.end())
					{
						localRemap[indices[i]] = localVertexCount++;
						registry.AddVertexMapIdx(baseVertexGlobal + indices[i]);
					}
					registry.AddIndex(localRemap[indices[i]]);
				}

				localTriangleCount++;
				trianglesProcessed++;
			}

			m.vertexCount   = localVertexCount;
			m.triangleCount = localTriangleCount;

			auto minBound = glm::vec3(1e10f);
			auto maxBound = glm::vec3(-1e10f);
			for (auto const& [globalIdx, localIdx] : localRemap)
			{
				minBound = glm::min(minBound, sphereVerts[globalIdx].position);
				maxBound = glm::max(maxBound, sphereVerts[globalIdx].position);
			}
			m.boundingCenter = (minBound + maxBound) * 0.5f;
			m.boundingRadius = glm::distance(maxBound, m.boundingCenter);

			registry.AddMeshlet(std::move(m));
		}

		auto info             = MeshInfo{};
		info.meshletBaseIndex = baseMeshletGlobal;
		info.meshletCount = static_cast<uint32_t>(registry.m_meshlets.Size()) - baseMeshletGlobal;
		info.materialID   = 0;

		return registry.AddInfo(std::move(info));
	}
}
