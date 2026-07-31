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
		case Format::RGB9E5_FLOAT:
			return MTL::PixelFormatRGB9E5Float;
		case Format::B5G6R5_UNORM:
			return MTL::PixelFormatB5G6R5Unorm;
		case Format::B5G5R5A1_UNORM:
			return MTL::PixelFormatBGR5A1Unorm;
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
		// Apple silicon has no Depth24Unorm_Stencil8.
		case Format::D24S8:
			return MTL::PixelFormatDepth32Float_Stencil8;
		case Format::X24G8_UINT:
		case Format::X32G8_UINT:
			return MTL::PixelFormatX32_Stencil8;
		case Format::BC1_UNORM:
			return MTL::PixelFormatBC1_RGBA;
		case Format::BC1_UNORM_SRGB:
			return MTL::PixelFormatBC1_RGBA_sRGB;
		case Format::BC2_UNORM:
			return MTL::PixelFormatBC2_RGBA;
		case Format::BC2_UNORM_SRGB:
			return MTL::PixelFormatBC2_RGBA_sRGB;
		case Format::BC3_UNORM:
			return MTL::PixelFormatBC3_RGBA;
		case Format::BC3_UNORM_SRGB:
			return MTL::PixelFormatBC3_RGBA_sRGB;
		case Format::BC4_UNORM:
			return MTL::PixelFormatBC4_RUnorm;
		case Format::BC4_SNORM:
			return MTL::PixelFormatBC4_RSnorm;
		case Format::BC5_UNORM:
			return MTL::PixelFormatBC5_RGUnorm;
		case Format::BC5_SNORM:
			return MTL::PixelFormatBC5_RGSnorm;
		case Format::BC6H_UFLOAT:
			return MTL::PixelFormatBC6H_RGBUfloat;
		case Format::BC6H_SFLOAT:
			return MTL::PixelFormatBC6H_RGBFloat;
		case Format::BC7_UNORM:
			return MTL::PixelFormatBC7_RGBAUnorm;
		case Format::BC7_UNORM_SRGB:
			return MTL::PixelFormatBC7_RGBAUnorm_sRGB;
		default:
			gfatal("Metal backend: unsupported Format {}", static_cast<int>(format));
		}
	}

	MTL::TextureType
	ConvertTextureType(TextureDimension dimension) noexcept
	{
		switch (dimension)
		{
		case TextureDimension::kTexture1D:
			return MTL::TextureType1D;
		case TextureDimension::kTexture1DArray:
			return MTL::TextureType1DArray;
		case TextureDimension::kTexture2D:
			return MTL::TextureType2D;
		case TextureDimension::kTexture2DArray:
			return MTL::TextureType2DArray;
		case TextureDimension::kTextureCube:
			return MTL::TextureTypeCube;
		case TextureDimension::kTextureCubeArray:
			return MTL::TextureTypeCubeArray;
		case TextureDimension::kTexture2DMS:
			return MTL::TextureType2DMultisample;
		case TextureDimension::kTexture2DMSArray:
			return MTL::TextureType2DMultisampleArray;
		case TextureDimension::kTexture3D:
			return MTL::TextureType3D;
		default:
			gfatal("Metal backend: unsupported TextureDimension {}", static_cast<int>(dimension));
		}
	}

	bool
	FormatHasStencil(Format format) noexcept
	{
		return format == Format::D24S8 || format == Format::D32S8;
	}

	MTL::BlendFactor
	ConvertBlendFactor(BlendFactor factor) noexcept
	{
		switch (factor)
		{
		case BlendFactor::kZero:
			return MTL::BlendFactorZero;
		case BlendFactor::kOne:
			return MTL::BlendFactorOne;
		case BlendFactor::kSrcColor:
			return MTL::BlendFactorSourceColor;
		case BlendFactor::kInvSrcColor:
			return MTL::BlendFactorOneMinusSourceColor;
		case BlendFactor::kSrcAlpha:
			return MTL::BlendFactorSourceAlpha;
		case BlendFactor::kInvSrcAlpha:
			return MTL::BlendFactorOneMinusSourceAlpha;
		case BlendFactor::kDstAlpha:
			return MTL::BlendFactorDestinationAlpha;
		case BlendFactor::kInvDstAlpha:
			return MTL::BlendFactorOneMinusDestinationAlpha;
		case BlendFactor::kDstColor:
			return MTL::BlendFactorDestinationColor;
		case BlendFactor::kInvDstColor:
			return MTL::BlendFactorOneMinusDestinationColor;
		case BlendFactor::kSrcAlphaSaturate:
			return MTL::BlendFactorSourceAlphaSaturated;
		case BlendFactor::kConstantColor:
			return MTL::BlendFactorBlendColor;
		case BlendFactor::kInvConstantColor:
			return MTL::BlendFactorOneMinusBlendColor;
		case BlendFactor::kSrc1Color:
			return MTL::BlendFactorSource1Color;
		case BlendFactor::kInvSrc1Color:
			return MTL::BlendFactorOneMinusSource1Color;
		case BlendFactor::kSrc1Alpha:
			return MTL::BlendFactorSource1Alpha;
		case BlendFactor::kInvSrc1Alpha:
			return MTL::BlendFactorOneMinusSource1Alpha;
		default:
			gfatal("Metal backend: unsupported BlendFactor {}", static_cast<int>(factor));
		}
	}

	MTL::BlendOperation
	ConvertBlendOp(BlendOp op) noexcept
	{
		switch (op)
		{
		case BlendOp::kAdd:
			return MTL::BlendOperationAdd;
		case BlendOp::kSubtract:
			return MTL::BlendOperationSubtract;
		case BlendOp::kReverseSubtract:
			return MTL::BlendOperationReverseSubtract;
		case BlendOp::kMin:
			return MTL::BlendOperationMin;
		case BlendOp::kMax:
			return MTL::BlendOperationMax;
		default:
			gfatal("Metal backend: unsupported BlendOp {}", static_cast<int>(op));
		}
	}

	MTL::ColorWriteMask
	ConvertColorWriteMask(ColorMask mask) noexcept
	{
		const auto bits = static_cast<uint8_t>(mask);
		auto       out  = static_cast<MTL::ColorWriteMask>(MTL::ColorWriteMaskNone);
		if (bits & static_cast<uint8_t>(ColorMask::kRed))
			out |= MTL::ColorWriteMaskRed;
		if (bits & static_cast<uint8_t>(ColorMask::kGreen))
			out |= MTL::ColorWriteMaskGreen;
		if (bits & static_cast<uint8_t>(ColorMask::kBlue))
			out |= MTL::ColorWriteMaskBlue;
		if (bits & static_cast<uint8_t>(ColorMask::kAlpha))
			out |= MTL::ColorWriteMaskAlpha;
		return out;
	}

	MTL::CompareFunction
	ConvertComparisonFunc(ComparisonFunc func) noexcept
	{
		switch (func)
		{
		case ComparisonFunc::kNever:
			return MTL::CompareFunctionNever;
		case ComparisonFunc::kLess:
			return MTL::CompareFunctionLess;
		case ComparisonFunc::kEqual:
			return MTL::CompareFunctionEqual;
		case ComparisonFunc::kLessOrEqual:
			return MTL::CompareFunctionLessEqual;
		case ComparisonFunc::kGreater:
			return MTL::CompareFunctionGreater;
		case ComparisonFunc::kNotEqual:
			return MTL::CompareFunctionNotEqual;
		case ComparisonFunc::kGreaterOrEqual:
			return MTL::CompareFunctionGreaterEqual;
		case ComparisonFunc::kAlways:
			return MTL::CompareFunctionAlways;
		default:
			gfatal("Metal backend: unsupported ComparisonFunc {}", static_cast<int>(func));
		}
	}

	MTL::StencilOperation
	ConvertStencilOp(StencilOp op) noexcept
	{
		switch (op)
		{
		case StencilOp::kKeep:
			return MTL::StencilOperationKeep;
		case StencilOp::kZero:
			return MTL::StencilOperationZero;
		case StencilOp::kReplace:
			return MTL::StencilOperationReplace;
		case StencilOp::kIncrementAndClamp:
			return MTL::StencilOperationIncrementClamp;
		case StencilOp::kDecrementAndClamp:
			return MTL::StencilOperationDecrementClamp;
		case StencilOp::kInvert:
			return MTL::StencilOperationInvert;
		case StencilOp::kIncrementAndWrap:
			return MTL::StencilOperationIncrementWrap;
		case StencilOp::kDecrementAndWrap:
			return MTL::StencilOperationDecrementWrap;
		default:
			gfatal("Metal backend: unsupported StencilOp {}", static_cast<int>(op));
		}
	}

	MTL::CullMode
	ConvertCullMode(RasterCullMode mode) noexcept
	{
		switch (mode)
		{
		case RasterCullMode::kNone:
			return MTL::CullModeNone;
		case RasterCullMode::kFront:
			return MTL::CullModeFront;
		case RasterCullMode::kBack:
			return MTL::CullModeBack;
		default:
			gfatal("Metal backend: unsupported RasterCullMode {}", static_cast<int>(mode));
		}
	}

	MTL::TriangleFillMode
	ConvertFillMode(RasterFillMode mode) noexcept
	{
		switch (mode)
		{
		case RasterFillMode::kSolid:
			return MTL::TriangleFillModeFill;
		case RasterFillMode::kWireframe:
			return MTL::TriangleFillModeLines;
		default:
			gfatal("Metal backend: unsupported RasterFillMode {}", static_cast<int>(mode));
		}
	}
}
