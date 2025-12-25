#include "material/Material.h"
#include <core/file/file.h>

gfx::Material::Material(nvrhi::DeviceHandle device, std::string_view pixelShaderPath)
{
	auto pixelShaderData = core::file::readFileBytes(pixelShaderPath);
	m_pixelShader        = device->createShader(
        nvrhi::ShaderDesc()
            .setShaderType(nvrhi::ShaderType::Pixel)
            .setDebugName(std::string{ pixelShaderPath }),
        pixelShaderData.data(),
        pixelShaderData.size());
}

nvrhi::ShaderHandle
gfx::Material::GetPixelShader() const noexcept
{
	return m_pixelShader;
}
