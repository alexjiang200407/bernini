#include "mesh/MeshFactory.h"

namespace gfx
{
	struct Vertex
	{
		glm::vec3 pos;
		glm::vec3 norm;
	};

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

	std::shared_ptr<Mesh::SharedData>
	MeshFactory::CreateCubeSharedData() const
	{
		auto shared = Mesh::CreateSharedData();

		auto desc = DynamicBufferDesc{};
		desc.AddElement("POSITION", ElementType::kFloat3)
			.AddElement("NORMAL", ElementType::kFloat3)
			.SetName("Cube Geometry Vertex Buffer");

		auto size           = static_cast<uint32_t>(std::size(cubeVertices));
		shared->vertexBuf   = std::move(DynamicVertexBuffer{ m_device, desc, size });
		shared->indexCount  = static_cast<uint32_t>(std::size(cubeIndices));
		auto indexBufSz     = sizeof(uint16_t) * shared->indexCount;
		shared->indexBuffer = m_device->createBuffer(
			nvrhi::BufferDesc{}
				.setByteSize(indexBufSz)
				.setIsIndexBuffer(true)
				.setInitialState(nvrhi::ResourceStates::CopyDest)
				.setKeepInitialState(false)
				.setDebugName("Primitive Geometry Index Buffer"));

		for (uint32_t i = 0; i < size; ++i)
		{
			shared->vertexBuf[i]["POSITION"] = cubeVertices[i].pos;
		}

		nvrhi::CommandListHandle uploadCmdList = m_device->createCommandList();
		uploadCmdList->open();
		shared->vertexBuf.Update(uploadCmdList);
		uploadCmdList->writeBuffer(shared->indexBuffer, cubeIndices, indexBufSz);
		uploadCmdList->close();
		m_device->executeCommandList(uploadCmdList);

		return shared;
	}
}
