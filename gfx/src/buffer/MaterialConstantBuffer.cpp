#include "buffer/MaterialConstantBuffer.h"
#include "shader_reflect/ShaderInput.h"

namespace
{
	using namespace gfx;

	DynamicConstantBufferDesc
	generateMaterialConstantBufDesc(std::string_view name, nvrhi::ShaderHandle shader)
	{
		auto desc = DynamicConstantBufferDesc{};
		desc.SetName(name).SetUpdateFrequency(UpdateFrequency::kPerMaterial);

		auto cbufInputs = getConstantBufferInputs(shader);
		for (const auto& cbufInput : cbufInputs)
		{
			if (cbufInput.name == name)
			{
				for (const auto& entry : cbufInput.entries)
				{
					desc.AddElement(entry.name, entry.type);
				}
			}
		}
		return desc;
	}

}

gfx::MaterialConstantBuffer::MaterialConstantBuffer(
	nvrhi::DeviceHandle device,
	std::string_view    name,
	nvrhi::ShaderHandle shader) :
	DynamicConstantBuffer{ device, generateMaterialConstantBufDesc(name, shader) }
{}
