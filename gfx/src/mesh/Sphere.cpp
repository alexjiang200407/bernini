#include "mesh/MeshFactory.h"

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
	BuildSphere(std::vector<SphereVertex>& vertices, std::vector<uint16_t>& indices)
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
				uint16_t i0 = uint16_t(stack * stride + slice);
				uint16_t i1 = uint16_t(i0 + stride);
				uint16_t i2 = uint16_t(i0 + 1);
				uint16_t i3 = uint16_t(i1 + 1);

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
	std::shared_ptr<Mesh::SharedData>
	MeshFactory::CreateSphereSharedData() const
	{
		auto retval   = Mesh::CreateSharedData();
		auto vertices = std::vector<SphereVertex>{};
		auto indices  = std::vector<uint16_t>{};

		BuildSphere(vertices, indices);

		DynamicBufferDesc desc{};
		desc.AddElement("POSITION", ElementType::kFloat3)
			.AddElement("NORMAL", ElementType::kFloat3)
			.SetName("Sphere Geometry Vertex Buffer");

		uint32_t            size = static_cast<uint32_t>(vertices.size());
		DynamicVertexBuffer vb{ m_device, std::move(desc), size };

		for (uint32_t i = 0; i < size; ++i)
		{
			vb[i]["POSITION"] = vertices[i].pos;
			vb[i]["NORMAL"]   = vertices[i].normal;
		}

		retval->vertexBuf   = std::move(vb);
		retval->indexCount  = static_cast<uint32_t>(indices.size());
		retval->indexBuffer = m_device->createBuffer(
			nvrhi::BufferDesc{}
				.setByteSize(static_cast<uint32_t>(sizeof(uint16_t) * indices.size()))
				.setIsIndexBuffer(true)
				.setInitialState(nvrhi::ResourceStates::IndexBuffer)
				.setKeepInitialState(true)
				.setDebugName("Sphere Geometry Index Buffer"));

		auto indexBufSz = sizeof(uint16_t) * retval->indexCount;

		auto uploadCmdList = m_device->createCommandList();

		uploadCmdList->open();
		retval->vertexBuf.Update(uploadCmdList);
		uploadCmdList->writeBuffer(retval->indexBuffer, indices.data(), indexBufSz);
		uploadCmdList->close();
		m_device->executeCommandList(uploadCmdList);

		return retval;
	}
}
