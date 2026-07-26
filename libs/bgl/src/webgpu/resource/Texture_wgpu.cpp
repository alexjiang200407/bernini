#include "resource/Texture_wgpu.h"

#include "convert_wgpu.h"

namespace bgl
{
	namespace
	{
		wgpu::TextureUsage
		ToWgpuUsage(TextureUsage usage) noexcept
		{
			// Copy in/out is always allowed: uploads (WriteTexture) and readback both need it.
			auto result = wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;

			if (usage.any(TextureUsageFlag::kSRV))
				result = result | wgpu::TextureUsage::TextureBinding;
			if (usage.any(TextureUsageFlag::kRenderTarget, TextureUsageFlag::kDepthStencil))
				result = result | wgpu::TextureUsage::RenderAttachment;

			return result;
		}
	}

	Texture::Texture(const wgpu::Device& device, const TextureDesc& desc) : m_Desc(desc)
	{
		gassert(device != nullptr, "Texture: null device");
		gassert(desc.width > 0 && desc.height > 0, "Texture '{}': zero extent", desc.debugName);

		auto wgpuDesc          = wgpu::TextureDescriptor{};
		wgpuDesc.label         = std::string_view(desc.debugName);
		wgpuDesc.dimension     = wgpu::TextureDimension::e2D;
		wgpuDesc.size          = { desc.width, desc.height, desc.arraySize };
		wgpuDesc.format        = ToWgpuTextureFormat(desc.format);
		wgpuDesc.mipLevelCount = desc.mipLevels;
		wgpuDesc.sampleCount   = desc.sampleCount;
		wgpuDesc.usage         = ToWgpuUsage(desc.usage);

		m_Texture = device.CreateTexture(&wgpuDesc);
	}
}
