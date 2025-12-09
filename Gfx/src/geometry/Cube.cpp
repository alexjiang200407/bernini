#include "geometry/Cube.h"
#include <core/file/file.h>

struct Vertex
{
	glm::vec3 pos;
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

namespace gfx::geom
{
	Cube::Cube(nvrhi::DeviceHandle& device)
	{
		{
			auto vertexBufferDesc = nvrhi::BufferDesc{};
			vertexBufferDesc.setByteSize(sizeof(cubeVertices))
				.setIsVertexBuffer(true)
				.setInitialState(nvrhi::ResourceStates::CopyDest)
				.setKeepInitialState(false)
				.setDebugName("Cube Vertex Buffer");

			vertexBuffer = device->createBuffer(vertexBufferDesc);
		}

		{
			nvrhi::BufferDesc indexDesc;
			indexDesc.setByteSize(sizeof(cubeIndices))
				.setIsIndexBuffer(true)
				.setInitialState(nvrhi::ResourceStates::CopyDest)
				.setKeepInitialState(false)
				.setDebugName("Cube Index Buffer");
			indexBuffer = device->createBuffer(indexDesc);
		}

		{
			nvrhi::CommandListHandle uploadCmdList = device->createCommandList();
			uploadCmdList->open();
			uploadCmdList->writeBuffer(vertexBuffer, cubeVertices, sizeof(cubeVertices));
			uploadCmdList->writeBuffer(indexBuffer, cubeIndices, sizeof(cubeIndices));
			uploadCmdList->close();
			device->executeCommandList(uploadCmdList);
		}

		auto vertexShaderData = core::file::readFileBytes("shaders/VS_cube.cso");
		auto pixelShaderData  = core::file::readFileBytes("shaders/PS_cube.cso");

		vertexShader = device->createShader(
			nvrhi::ShaderDesc{}
				.setShaderType(nvrhi::ShaderType::Vertex)
				.setDebugName("Cube Vertex Shader"),
			vertexShaderData.data(),
			vertexShaderData.size());

		nvrhi::VertexAttributeDesc attributes[] = { nvrhi::VertexAttributeDesc{}
			                                            .setName("POSITION")
			                                            .setFormat(nvrhi::Format::RGB32_FLOAT)
			                                            .setOffset(0)
			                                            .setBufferIndex(0)
			                                            .setElementStride(sizeof(Vertex)) };

		inputLayout = device->createInputLayout(
			attributes,
			static_cast<uint32_t>(std::size(attributes)),
			vertexShader);

		pixelShader = device->createShader(
			nvrhi::ShaderDesc()
				.setShaderType(nvrhi::ShaderType::Pixel)
				.setDebugName("Cube Pixel Shader"),
			pixelShaderData.data(),
			pixelShaderData.size());
	}

	void
	Cube::Draw(nvrhi::CommandListHandle cmdList, nvrhi::GraphicsState& state) const
	{
		nvrhi::VertexBufferBinding vbufBinding;
		vbufBinding.buffer = vertexBuffer;
		vbufBinding.slot   = 0;
		vbufBinding.offset = 0;

		nvrhi::IndexBufferBinding ibufBinding;
		ibufBinding.buffer = indexBuffer;
		ibufBinding.format = nvrhi::Format::R16_UINT;
		ibufBinding.offset = 0;

		state.addVertexBuffer(vbufBinding);
		state.setIndexBuffer(ibufBinding);

		cmdList->setGraphicsState(state);

		cmdList->drawIndexed(nvrhi::DrawArguments{}
		                         .setVertexCount(static_cast<uint32_t>(std::size(cubeVertices)))
		                         .setStartIndexLocation(0)
		                         .setInstanceCount(1));
	}
}
