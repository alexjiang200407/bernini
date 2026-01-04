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
	Mesh::InfoID
	MeshFactory::CreateSphereInfo(MeshRegistry& registry)
	{
		auto vertices = std::vector<SphereVertex>{};
		auto indices  = std::vector<uint32_t>{};
		BuildSphere(vertices, indices);

		const auto baseVertex = registry.m_vertices.Size();
		const auto startIndex = registry.m_indices.Size();

		for (const auto& v : vertices)
		{
			registry.AddVertex(v.pos, v.normal, glm::vec2{ 0.0f, 0.0f });
		}

		for (auto idx : indices)
		{
			registry.AddIndex(static_cast<uint32_t>(idx));
		}

		return registry.AddInfo(startIndex, indices.size(), baseVertex);
	}
}
