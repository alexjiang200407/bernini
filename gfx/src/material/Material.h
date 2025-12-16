#pragma once

namespace gfx
{
	class Material
	{
	public:
		Material(nvrhi::DeviceHandle device, std::string_view pixelShaderPath);

		nvrhi::ShaderHandle
		GetPixelShader() const noexcept;

	private:
		nvrhi::ShaderHandle m_pixelShader;
	};
}
