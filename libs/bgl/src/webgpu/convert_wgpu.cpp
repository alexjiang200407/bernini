#include "convert_wgpu.h"

namespace bgl
{
	// Exhaustive by convention (as convert_d3d12 is): MSVC's C4061 flags any Format enumerator not
	// given an explicit case even with a default, so the formats WebGPU has no texture equivalent for
	// (three-component, BGRX, the 5/6-bit packed and X-plane formats) fall through to one gfatal.
	wgpu::TextureFormat
	ToWgpuTextureFormat(Format format) noexcept
	{
		using F = wgpu::TextureFormat;

		switch (format)
		{
		case Format::R8_UINT:
			return F::R8Uint;
		case Format::R8_SINT:
			return F::R8Sint;
		case Format::R8_UNORM:
			return F::R8Unorm;
		case Format::R8_SNORM:
			return F::R8Snorm;
		case Format::RG8_UINT:
			return F::RG8Uint;
		case Format::RG8_SINT:
			return F::RG8Sint;
		case Format::RG8_UNORM:
			return F::RG8Unorm;
		case Format::RG8_SNORM:
			return F::RG8Snorm;
		case Format::R16_UINT:
			return F::R16Uint;
		case Format::R16_SINT:
			return F::R16Sint;
		case Format::R16_UNORM:
			return F::R16Unorm;
		case Format::R16_SNORM:
			return F::R16Snorm;
		case Format::R16_FLOAT:
			return F::R16Float;
		case Format::RGBA8_UINT:
			return F::RGBA8Uint;
		case Format::RGBA8_SINT:
			return F::RGBA8Sint;
		case Format::RGBA8_UNORM:
			return F::RGBA8Unorm;
		case Format::RGBA8_SNORM:
			return F::RGBA8Snorm;
		case Format::BGRA8_UNORM:
			return F::BGRA8Unorm;
		case Format::SRGBA8_UNORM:
			return F::RGBA8UnormSrgb;
		case Format::SBGRA8_UNORM:
			return F::BGRA8UnormSrgb;
		case Format::R10G10B10A2_UNORM:
			return F::RGB10A2Unorm;
		case Format::R11G11B10_FLOAT:
			return F::RG11B10Ufloat;
		case Format::RG16_UINT:
			return F::RG16Uint;
		case Format::RG16_SINT:
			return F::RG16Sint;
		case Format::RG16_UNORM:
			return F::RG16Unorm;
		case Format::RG16_SNORM:
			return F::RG16Snorm;
		case Format::RG16_FLOAT:
			return F::RG16Float;
		case Format::R32_UINT:
			return F::R32Uint;
		case Format::R32_SINT:
			return F::R32Sint;
		case Format::R32_FLOAT:
			return F::R32Float;
		case Format::RGBA16_UINT:
			return F::RGBA16Uint;
		case Format::RGBA16_SINT:
			return F::RGBA16Sint;
		case Format::RGBA16_UNORM:
			return F::RGBA16Unorm;
		case Format::RGBA16_SNORM:
			return F::RGBA16Snorm;
		case Format::RGBA16_FLOAT:
			return F::RGBA16Float;
		case Format::RG32_UINT:
			return F::RG32Uint;
		case Format::RG32_SINT:
			return F::RG32Sint;
		case Format::RG32_FLOAT:
			return F::RG32Float;
		case Format::RGBA32_UINT:
			return F::RGBA32Uint;
		case Format::RGBA32_SINT:
			return F::RGBA32Sint;
		case Format::RGBA32_FLOAT:
			return F::RGBA32Float;
		case Format::D16:
			return F::Depth16Unorm;
		// The D24S8 the engine hardcodes maps to Depth24PlusStencil8 (see the Metal depth note).
		case Format::D24S8:
			return F::Depth24PlusStencil8;
		case Format::D32:
			return F::Depth32Float;
		case Format::D32S8:
			return F::Depth32FloatStencil8;
		case Format::BC1_UNORM:
			return F::BC1RGBAUnorm;
		case Format::BC1_UNORM_SRGB:
			return F::BC1RGBAUnormSrgb;
		case Format::BC2_UNORM:
			return F::BC2RGBAUnorm;
		case Format::BC2_UNORM_SRGB:
			return F::BC2RGBAUnormSrgb;
		case Format::BC3_UNORM:
			return F::BC3RGBAUnorm;
		case Format::BC3_UNORM_SRGB:
			return F::BC3RGBAUnormSrgb;
		case Format::BC4_UNORM:
			return F::BC4RUnorm;
		case Format::BC4_SNORM:
			return F::BC4RSnorm;
		case Format::BC5_UNORM:
			return F::BC5RGUnorm;
		case Format::BC5_SNORM:
			return F::BC5RGSnorm;
		case Format::BC6H_UFLOAT:
			return F::BC6HRGBUfloat;
		case Format::BC6H_SFLOAT:
			return F::BC6HRGBFloat;
		case Format::BC7_UNORM:
			return F::BC7RGBAUnorm;
		case Format::BC7_UNORM_SRGB:
			return F::BC7RGBAUnormSrgb;

		// No WebGPU texture equivalent.
		case Format::UNKNOWN:
		case Format::BGRA4_UNORM:
		case Format::B5G6R5_UNORM:
		case Format::B5G5R5A1_UNORM:
		case Format::BGRX8_UNORM:
		case Format::SBGRX8_UNORM:
		case Format::X24G8_UINT:
		case Format::X32G8_UINT:
		case Format::RGB32_UINT:
		case Format::RGB32_SINT:
		case Format::RGB32_FLOAT:
		case Format::COUNT:
			gfatal("wgpu: no texture format for {}", static_cast<int>(format));
		}

		gfatal("wgpu: unknown texture format {}", static_cast<int>(format));
	}
}
