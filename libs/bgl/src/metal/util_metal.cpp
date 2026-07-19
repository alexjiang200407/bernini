#include "util_metal.h"

namespace bgl
{
	MTL::PixelFormat
	ConvertFormat(Format format) noexcept
	{
		switch (format)
		{
		case Format::R8_UINT:
			return MTL::PixelFormatR8Uint;
		case Format::R8_SINT:
			return MTL::PixelFormatR8Sint;
		case Format::R8_UNORM:
			return MTL::PixelFormatR8Unorm;
		case Format::R8_SNORM:
			return MTL::PixelFormatR8Snorm;
		case Format::RG8_UINT:
			return MTL::PixelFormatRG8Uint;
		case Format::RG8_SINT:
			return MTL::PixelFormatRG8Sint;
		case Format::RG8_UNORM:
			return MTL::PixelFormatRG8Unorm;
		case Format::RG8_SNORM:
			return MTL::PixelFormatRG8Snorm;
		case Format::R16_UINT:
			return MTL::PixelFormatR16Uint;
		case Format::R16_SINT:
			return MTL::PixelFormatR16Sint;
		case Format::R16_UNORM:
			return MTL::PixelFormatR16Unorm;
		case Format::R16_SNORM:
			return MTL::PixelFormatR16Snorm;
		case Format::R16_FLOAT:
			return MTL::PixelFormatR16Float;
		case Format::RGBA8_UINT:
			return MTL::PixelFormatRGBA8Uint;
		case Format::RGBA8_SINT:
			return MTL::PixelFormatRGBA8Sint;
		case Format::RGBA8_UNORM:
			return MTL::PixelFormatRGBA8Unorm;
		case Format::RGBA8_SNORM:
			return MTL::PixelFormatRGBA8Snorm;
		case Format::SRGBA8_UNORM:
			return MTL::PixelFormatRGBA8Unorm_sRGB;
		case Format::BGRA8_UNORM:
		case Format::BGRX8_UNORM:
			return MTL::PixelFormatBGRA8Unorm;
		case Format::SBGRA8_UNORM:
		case Format::SBGRX8_UNORM:
			return MTL::PixelFormatBGRA8Unorm_sRGB;
		case Format::R10G10B10A2_UNORM:
			return MTL::PixelFormatRGB10A2Unorm;
		case Format::R11G11B10_FLOAT:
			return MTL::PixelFormatRG11B10Float;
		case Format::RG16_UINT:
			return MTL::PixelFormatRG16Uint;
		case Format::RG16_SINT:
			return MTL::PixelFormatRG16Sint;
		case Format::RG16_UNORM:
			return MTL::PixelFormatRG16Unorm;
		case Format::RG16_SNORM:
			return MTL::PixelFormatRG16Snorm;
		case Format::RG16_FLOAT:
			return MTL::PixelFormatRG16Float;
		case Format::R32_UINT:
			return MTL::PixelFormatR32Uint;
		case Format::R32_SINT:
			return MTL::PixelFormatR32Sint;
		case Format::R32_FLOAT:
			return MTL::PixelFormatR32Float;
		case Format::RGBA16_UINT:
			return MTL::PixelFormatRGBA16Uint;
		case Format::RGBA16_SINT:
			return MTL::PixelFormatRGBA16Sint;
		case Format::RGBA16_UNORM:
			return MTL::PixelFormatRGBA16Unorm;
		case Format::RGBA16_SNORM:
			return MTL::PixelFormatRGBA16Snorm;
		case Format::RGBA16_FLOAT:
			return MTL::PixelFormatRGBA16Float;
		case Format::RG32_UINT:
			return MTL::PixelFormatRG32Uint;
		case Format::RG32_SINT:
			return MTL::PixelFormatRG32Sint;
		case Format::RG32_FLOAT:
			return MTL::PixelFormatRG32Float;
		case Format::RGBA32_UINT:
			return MTL::PixelFormatRGBA32Uint;
		case Format::RGBA32_SINT:
			return MTL::PixelFormatRGBA32Sint;
		case Format::RGBA32_FLOAT:
			return MTL::PixelFormatRGBA32Float;
		case Format::D16:
			return MTL::PixelFormatDepth16Unorm;
		case Format::D32:
			return MTL::PixelFormatDepth32Float;
		case Format::D32S8:
			return MTL::PixelFormatDepth32Float_Stencil8;
		default:
			gfatal("Metal backend: unsupported Format {}", static_cast<int>(format));
		}
	}

	uint32_t
	FormatBytesPerPixel(Format format) noexcept
	{
		switch (format)
		{
		case Format::R8_UINT:
		case Format::R8_SINT:
		case Format::R8_UNORM:
		case Format::R8_SNORM:
			return 1;
		case Format::RG8_UINT:
		case Format::RG8_SINT:
		case Format::RG8_UNORM:
		case Format::RG8_SNORM:
		case Format::R16_UINT:
		case Format::R16_SINT:
		case Format::R16_UNORM:
		case Format::R16_SNORM:
		case Format::R16_FLOAT:
		case Format::D16:
			return 2;
		case Format::RGBA8_UINT:
		case Format::RGBA8_SINT:
		case Format::RGBA8_UNORM:
		case Format::RGBA8_SNORM:
		case Format::SRGBA8_UNORM:
		case Format::BGRA8_UNORM:
		case Format::BGRX8_UNORM:
		case Format::SBGRA8_UNORM:
		case Format::SBGRX8_UNORM:
		case Format::R10G10B10A2_UNORM:
		case Format::R11G11B10_FLOAT:
		case Format::RG16_UINT:
		case Format::RG16_SINT:
		case Format::RG16_UNORM:
		case Format::RG16_SNORM:
		case Format::RG16_FLOAT:
		case Format::R32_UINT:
		case Format::R32_SINT:
		case Format::R32_FLOAT:
		case Format::D32:
			return 4;
		case Format::RGBA16_UINT:
		case Format::RGBA16_SINT:
		case Format::RGBA16_UNORM:
		case Format::RGBA16_SNORM:
		case Format::RGBA16_FLOAT:
		case Format::RG32_UINT:
		case Format::RG32_SINT:
		case Format::RG32_FLOAT:
			return 8;
		case Format::RGBA32_UINT:
		case Format::RGBA32_SINT:
		case Format::RGBA32_FLOAT:
			return 16;
		default:
			gfatal("Metal backend: no byte-per-pixel size for Format {}", static_cast<int>(format));
		}
	}
}
