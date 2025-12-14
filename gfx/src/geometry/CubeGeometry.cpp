#include "geometry/CubeGeometry.h"

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

namespace gfx
{
	CubeGeometry::CubeGeometry(nvrhi::DeviceHandle device, std::string_view vertexShaderPath) :
		PrimitiveGeometry<CubeGeometry>{ device, vertexShaderPath }
	{}

	DynamicVertexBuffer
	CubeGeometry::GetVertexBuffer(nvrhi::DeviceHandle device) noexcept
	{
		auto desc = DynamicBufferDesc{};
		desc.AddElement("POSITION", ElementType::kFloat3)
			.AddElement("NORMAL", ElementType::kFloat3)
			.SetName("Cube Geometry Vertex Buffer");

		auto size         = static_cast<uint32_t>(std::size(cubeVertices));
		auto vertexBuffer = std::move(DynamicVertexBuffer{ device, desc, size });

		for (uint32_t i = 0; i < size; ++i)
		{
			vertexBuffer[i]["POSITION"] = cubeVertices[i].pos;
		}

		return vertexBuffer;
	}

	std::span<const uint16_t>
	CubeGeometry::GetIndices() noexcept
	{
		return std::span<const uint16_t>(cubeIndices);
	}
}
