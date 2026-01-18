#pragma once
#include "BindingSlots.h"
#include "buffer/DynamicConstantBuffer.h"

namespace gfx
{
	class Material final
	{
	public:
		//static constexpr auto

	public:
		Material(nvrhi::DeviceHandle device, std::string_view pixelShaderPath);

		nvrhi::ShaderHandle
		GetPixelShader() const noexcept;

	private:
		DynamicConstantBuffer                           m_materialCB{};
		nvrhi::ShaderHandle                             m_pixelShader{};
		std::array<nvrhi::TextureHandle, TEXTURE_COUNT> m_textures{};
	};
}
