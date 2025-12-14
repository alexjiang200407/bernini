#pragma once
#include "buffer/DynamicVertexBuffer.h"
#include "geometry/PrimitiveGeometry.h"

namespace gfx
{
	class CubeGeometry : public PrimitiveGeometry<CubeGeometry>
	{
	public:
		CubeGeometry(nvrhi::DeviceHandle device, std::string_view vertexShaderPath);

		static DynamicVertexBuffer
		GetVertexBuffer(nvrhi::DeviceHandle device) noexcept;

		static std::span<const uint16_t>
		GetIndices() noexcept;
	};
}
