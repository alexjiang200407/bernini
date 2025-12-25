#pragma once
#include "buffer/DynamicConstantBuffer.h"

namespace gfx
{
	class MaterialConstantBuffer : public DynamicConstantBuffer
	{
	public:
		MaterialConstantBuffer() noexcept = default;
		MaterialConstantBuffer(nvrhi::DeviceHandle device, std::string_view shader);
	};
}
