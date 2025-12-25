#include "buffer/MaterialConstantBuffer.h"
#include "BindingSlots.h"
#include "shader_reflect/ShaderInput.h"
#include <core/file/file.h>

namespace gfx
{
	MaterialConstantBuffer::MaterialConstantBuffer(
		nvrhi::DeviceHandle device,
		std::string_view    pixelShaderPath)
	{
		auto shaderByteCode = core::file::readFileBytes(pixelShaderPath);
		auto desc = getDynamicConstantBufferDesc(shaderByteCode, BindingSlots::PerMaterialSpace);
		desc.SetUpdateFrequency(UpdateFrequency::kPerMaterial);

		Init(device, desc);
	}
}
