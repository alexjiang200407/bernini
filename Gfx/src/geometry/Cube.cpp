#include "geometry/Cube.h"
#include <Core/file/file.h>

struct Vertex
{
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
};

Vertex cubeVertices[] = {
	// FRONT (+Z)
	{ { -1, -1, 1 }, { 0, 0, 1 }, { 0, 1 } },
	{ { 1, -1, 1 }, { 0, 0, 1 }, { 1, 1 } },
	{ { 1, 1, 1 }, { 0, 0, 1 }, { 1, 0 } },
	{ { -1, -1, 1 }, { 0, 0, 1 }, { 0, 1 } },
	{ { 1, 1, 1 }, { 0, 0, 1 }, { 1, 0 } },
	{ { -1, 1, 1 }, { 0, 0, 1 }, { 0, 0 } },

	// BACK (–Z)
	{ { 1, -1, -1 }, { 0, 0, -1 }, { 0, 1 } },
	{ { -1, -1, -1 }, { 0, 0, -1 }, { 1, 1 } },
	{ { -1, 1, -1 }, { 0, 0, -1 }, { 1, 0 } },
	{ { 1, -1, -1 }, { 0, 0, -1 }, { 0, 1 } },
	{ { -1, 1, -1 }, { 0, 0, -1 }, { 1, 0 } },
	{ { 1, 1, -1 }, { 0, 0, -1 }, { 0, 0 } },

	// LEFT (–X)
	{ { -1, -1, -1 }, { -1, 0, 0 }, { 0, 1 } },
	{ { -1, -1, 1 }, { -1, 0, 0 }, { 1, 1 } },
	{ { -1, 1, 1 }, { -1, 0, 0 }, { 1, 0 } },
	{ { -1, -1, -1 }, { -1, 0, 0 }, { 0, 1 } },
	{ { -1, 1, 1 }, { -1, 0, 0 }, { 1, 0 } },
	{ { -1, 1, -1 }, { -1, 0, 0 }, { 0, 0 } },

	// RIGHT (+X)
	{ { 1, -1, 1 }, { 1, 0, 0 }, { 0, 1 } },
	{ { 1, -1, -1 }, { 1, 0, 0 }, { 1, 1 } },
	{ { 1, 1, -1 }, { 1, 0, 0 }, { 1, 0 } },
	{ { 1, -1, 1 }, { 1, 0, 0 }, { 0, 1 } },
	{ { 1, 1, -1 }, { 1, 0, 0 }, { 1, 0 } },
	{ { 1, 1, 1 }, { 1, 0, 0 }, { 0, 0 } },

	// TOP (+Y)
	{ { -1, 1, 1 }, { 0, 1, 0 }, { 0, 1 } },
	{ { 1, 1, 1 }, { 0, 1, 0 }, { 1, 1 } },
	{ { 1, 1, -1 }, { 0, 1, 0 }, { 1, 0 } },
	{ { -1, 1, 1 }, { 0, 1, 0 }, { 0, 1 } },
	{ { 1, 1, -1 }, { 0, 1, 0 }, { 1, 0 } },
	{ { -1, 1, -1 }, { 0, 1, 0 }, { 0, 0 } },

	// BOTTOM (–Y)
	{ { -1, -1, -1 }, { 0, -1, 0 }, { 0, 1 } },
	{ { 1, -1, -1 }, { 0, -1, 0 }, { 1, 1 } },
	{ { 1, -1, 1 }, { 0, -1, 0 }, { 1, 0 } },
	{ { -1, -1, -1 }, { 0, -1, 0 }, { 0, 1 } },
	{ { 1, -1, 1 }, { 0, -1, 0 }, { 1, 0 } },
	{ { -1, -1, 1 }, { 0, -1, 0 }, { 0, 0 } },
};

namespace gfx::geom
{
	Cube::Cube(nvrhi::DeviceHandle& device)
	{
		auto vertexBufferDesc = nvrhi::BufferDesc{};

		vertexBufferDesc.setByteSize(sizeof(cubeVertices))
			.setIsVertexBuffer(true)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer)
			.setDebugName("Cube Vertex Buffer");

		vertexBuffer = device->createBuffer(vertexBufferDesc);

		auto vertexShaderData = core::file::readFileBytes("shaders/VertexShader_cube.cso");
		auto pixelShaderData  = core::file::readFileBytes("shaders/PixelShader_cube.cso");

		vertexShader = device->createShader(
			nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex),
			vertexShaderData.data(),
			vertexShaderData.size());

		nvrhi::VertexAttributeDesc attributes[] = { nvrhi::VertexAttributeDesc()
			                                            .setName("POSITION")
			                                            .setFormat(nvrhi::Format::RGB32_FLOAT)
			                                            .setOffset(offsetof(Vertex, pos))
			                                            .setElementStride(sizeof(Vertex)) };

		inputLayout =
			device->createInputLayout(attributes, uint32_t(std::size(attributes)), vertexShader);

		pixelShader = device->createShader(
			nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel),
			pixelShaderData.data(),
			pixelShaderData.size());
	};
}
