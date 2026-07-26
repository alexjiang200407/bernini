#include "convert_wgpu.h"

namespace bgl
{
	wgpu::TextureFormat
	ToWgpuTextureFormat(Format format) noexcept
	{
		switch (format)
		{
		case Format::R8_UNORM:
			return wgpu::TextureFormat::R8Unorm;
		case Format::RG8_UNORM:
			return wgpu::TextureFormat::RG8Unorm;
		case Format::RGBA8_UNORM:
			return wgpu::TextureFormat::RGBA8Unorm;
		case Format::SRGBA8_UNORM:
			return wgpu::TextureFormat::RGBA8UnormSrgb;
		case Format::BGRA8_UNORM:
			return wgpu::TextureFormat::BGRA8Unorm;
		case Format::SBGRA8_UNORM:
			return wgpu::TextureFormat::BGRA8UnormSrgb;
		case Format::R16_FLOAT:
			return wgpu::TextureFormat::R16Float;
		case Format::RG16_FLOAT:
			return wgpu::TextureFormat::RG16Float;
		case Format::RGBA16_FLOAT:
			return wgpu::TextureFormat::RGBA16Float;
		case Format::R32_FLOAT:
			return wgpu::TextureFormat::R32Float;
		case Format::R32_UINT:
			return wgpu::TextureFormat::R32Uint;
		case Format::RGBA32_FLOAT:
			return wgpu::TextureFormat::RGBA32Float;
		case Format::R10G10B10A2_UNORM:
			return wgpu::TextureFormat::RGB10A2Unorm;
		case Format::R11G11B10_FLOAT:
			return wgpu::TextureFormat::RG11B10Ufloat;
		// Depth: the D24S8 the engine hardcodes has no Apple-silicon equivalent, so it maps to
		// Depth24PlusStencil8 (see the Metal note in docs and the W3 depth decision).
		case Format::D32:
			return wgpu::TextureFormat::Depth32Float;
		case Format::D32S8:
			return wgpu::TextureFormat::Depth32FloatStencil8;
		case Format::D24S8:
			return wgpu::TextureFormat::Depth24PlusStencil8;
		default:
			gfatal("wgpu: unmapped texture format {}", static_cast<int>(format));
		}
	}
}
