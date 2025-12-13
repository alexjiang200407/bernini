#pragma once
#include "buffer/DynamicBuffer.h"
#include "shader_reflect/ShaderInput.h"

namespace gfx
{
	class DynamicVertexBuffer : public DynamicBuffer
	{
	public:
		DynamicVertexBuffer() noexcept = default;
		DynamicVertexBuffer(
			nvrhi::DeviceHandle      device,
			const DynamicBufferDesc& elementDesc,
			uint32_t                 count);

		nvrhi::InputLayoutHandle
		GenerateVertexLayout(nvrhi::DeviceHandle device, nvrhi::ShaderHandle vertexShader) const;
	};
}
