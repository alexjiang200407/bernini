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
		case Format::RGB9E5_FLOAT:
			return F::RGB9E5Ufloat;
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

	wgpu::CullMode
	ToWgpuCullMode(RasterCullMode mode) noexcept
	{
		switch (mode)
		{
		case RasterCullMode::kNone:
			return wgpu::CullMode::None;
		case RasterCullMode::kFront:
			return wgpu::CullMode::Front;
		case RasterCullMode::kBack:
			return wgpu::CullMode::Back;
		}

		gfatal("wgpu: unknown cull mode {}", static_cast<int>(mode));
	}

	wgpu::FrontFace
	ToWgpuFrontFace(bool frontCounterClockwise) noexcept
	{
		return frontCounterClockwise ? wgpu::FrontFace::CCW : wgpu::FrontFace::CW;
	}

	wgpu::CompareFunction
	ToWgpuCompareFunction(ComparisonFunc func) noexcept
	{
		switch (func)
		{
		case ComparisonFunc::kNever:
			return wgpu::CompareFunction::Never;
		case ComparisonFunc::kLess:
			return wgpu::CompareFunction::Less;
		case ComparisonFunc::kEqual:
			return wgpu::CompareFunction::Equal;
		case ComparisonFunc::kLessOrEqual:
			return wgpu::CompareFunction::LessEqual;
		case ComparisonFunc::kGreater:
			return wgpu::CompareFunction::Greater;
		case ComparisonFunc::kNotEqual:
			return wgpu::CompareFunction::NotEqual;
		case ComparisonFunc::kGreaterOrEqual:
			return wgpu::CompareFunction::GreaterEqual;
		case ComparisonFunc::kAlways:
			return wgpu::CompareFunction::Always;
		}

		gfatal("wgpu: unknown comparison func {}", static_cast<int>(func));
	}

	wgpu::StencilOperation
	ToWgpuStencilOperation(StencilOp op) noexcept
	{
		switch (op)
		{
		case StencilOp::kKeep:
			return wgpu::StencilOperation::Keep;
		case StencilOp::kZero:
			return wgpu::StencilOperation::Zero;
		case StencilOp::kReplace:
			return wgpu::StencilOperation::Replace;
		case StencilOp::kIncrementAndClamp:
			return wgpu::StencilOperation::IncrementClamp;
		case StencilOp::kDecrementAndClamp:
			return wgpu::StencilOperation::DecrementClamp;
		case StencilOp::kInvert:
			return wgpu::StencilOperation::Invert;
		case StencilOp::kIncrementAndWrap:
			return wgpu::StencilOperation::IncrementWrap;
		case StencilOp::kDecrementAndWrap:
			return wgpu::StencilOperation::DecrementWrap;
		}

		gfatal("wgpu: unknown stencil op {}", static_cast<int>(op));
	}

	wgpu::BlendFactor
	ToWgpuBlendFactor(BlendFactor factor) noexcept
	{
		switch (factor)
		{
		case BlendFactor::kZero:
			return wgpu::BlendFactor::Zero;
		case BlendFactor::kOne:
			return wgpu::BlendFactor::One;
		case BlendFactor::kSrcColor:
			return wgpu::BlendFactor::Src;
		case BlendFactor::kInvSrcColor:
			return wgpu::BlendFactor::OneMinusSrc;
		case BlendFactor::kSrcAlpha:
			return wgpu::BlendFactor::SrcAlpha;
		case BlendFactor::kInvSrcAlpha:
			return wgpu::BlendFactor::OneMinusSrcAlpha;
		case BlendFactor::kDstAlpha:
			return wgpu::BlendFactor::DstAlpha;
		case BlendFactor::kInvDstAlpha:
			return wgpu::BlendFactor::OneMinusDstAlpha;
		case BlendFactor::kDstColor:
			return wgpu::BlendFactor::Dst;
		case BlendFactor::kInvDstColor:
			return wgpu::BlendFactor::OneMinusDst;
		case BlendFactor::kSrcAlphaSaturate:
			return wgpu::BlendFactor::SrcAlphaSaturated;
		case BlendFactor::kConstantColor:
			return wgpu::BlendFactor::Constant;
		case BlendFactor::kInvConstantColor:
			return wgpu::BlendFactor::OneMinusConstant;
		case BlendFactor::kSrc1Color:
			return wgpu::BlendFactor::Src1;
		case BlendFactor::kInvSrc1Color:
			return wgpu::BlendFactor::OneMinusSrc1;
		case BlendFactor::kSrc1Alpha:
			return wgpu::BlendFactor::Src1Alpha;
		case BlendFactor::kInvSrc1Alpha:
			return wgpu::BlendFactor::OneMinusSrc1Alpha;
		}

		gfatal("wgpu: unknown blend factor {}", static_cast<int>(factor));
	}

	wgpu::BlendOperation
	ToWgpuBlendOperation(BlendOp op) noexcept
	{
		switch (op)
		{
		case BlendOp::kAdd:
			return wgpu::BlendOperation::Add;
		case BlendOp::kSubtract:
			return wgpu::BlendOperation::Subtract;
		case BlendOp::kReverseSubtract:
			return wgpu::BlendOperation::ReverseSubtract;
		case BlendOp::kMin:
			return wgpu::BlendOperation::Min;
		case BlendOp::kMax:
			return wgpu::BlendOperation::Max;
		}

		gfatal("wgpu: unknown blend op {}", static_cast<int>(op));
	}

	wgpu::ColorWriteMask
	ToWgpuColorWriteMask(ColorMask mask) noexcept
	{
		const auto bits = static_cast<uint8_t>(mask);

		auto result = wgpu::ColorWriteMask::None;
		if (bits & static_cast<uint8_t>(ColorMask::kRed))
			result = result | wgpu::ColorWriteMask::Red;
		if (bits & static_cast<uint8_t>(ColorMask::kGreen))
			result = result | wgpu::ColorWriteMask::Green;
		if (bits & static_cast<uint8_t>(ColorMask::kBlue))
			result = result | wgpu::ColorWriteMask::Blue;
		if (bits & static_cast<uint8_t>(ColorMask::kAlpha))
			result = result | wgpu::ColorWriteMask::Alpha;

		return result;
	}

	wgpu::AddressMode
	ToWgpuAddressMode(SamplerAddressMode mode) noexcept
	{
		switch (mode)
		{
		case SamplerAddressMode::kClamp:
			return wgpu::AddressMode::ClampToEdge;
		case SamplerAddressMode::kWrap:
			return wgpu::AddressMode::Repeat;
		case SamplerAddressMode::kMirror:
			return wgpu::AddressMode::MirrorRepeat;

		// No WebGPU address mode equivalent.
		case SamplerAddressMode::kBorder:
		case SamplerAddressMode::kMirrorOnce:
			gfatal("wgpu: no address mode for {}", static_cast<int>(mode));
		}

		gfatal("wgpu: unknown address mode {}", static_cast<int>(mode));
	}

	wgpu::SamplerDescriptor
	ToWgpuSamplerDescriptor(const SamplerDesc& desc) noexcept
	{
		if (desc.mipBias != 0.f)
			gfatal(
				"wgpu: a sampler has no LOD bias, so mipBias {} cannot be honoured",
				desc.mipBias);

		auto out         = wgpu::SamplerDescriptor{};
		out.addressModeU = ToWgpuAddressMode(desc.addressU);
		out.addressModeV = ToWgpuAddressMode(desc.addressV);
		out.addressModeW = ToWgpuAddressMode(desc.addressW);
		out.minFilter    = desc.minFilter ? wgpu::FilterMode::Linear : wgpu::FilterMode::Nearest;
		out.magFilter    = desc.magFilter ? wgpu::FilterMode::Linear : wgpu::FilterMode::Nearest;
		out.mipmapFilter =
			desc.mipFilter ? wgpu::MipmapFilterMode::Linear : wgpu::MipmapFilterMode::Nearest;
		out.maxAnisotropy = static_cast<uint16_t>(desc.maxAnisotropy);

		switch (desc.reductionType)
		{
		case SamplerReductionType::kStandard:
			break;
		case SamplerReductionType::kComparison:
			// SamplerDesc carries no comparison function; D3D12 hardcodes the same one.
			out.compare = wgpu::CompareFunction::Less;
			break;

		// No WebGPU reduction equivalent.
		case SamplerReductionType::kMinimum:
		case SamplerReductionType::kMaximum:
			gfatal("wgpu: no reduction type for {}", static_cast<int>(desc.reductionType));
		}

		// D3D12 encodes an anisotropic filter and ignores the three filter flags; WebGPU rejects the
		// sampler unless all three are linear.
		gassert(
			out.maxAnisotropy <= 1 || (desc.minFilter && desc.magFilter && desc.mipFilter),
			"An anisotropic sampler must filter linearly on all three axes");

		// D3D12 clamps to FLT_MAX; 32 is the WebGPU default and covers any mip chain it can build.
		out.lodMinClamp = 0.f;
		out.lodMaxClamp = 32.f;

		return out;
	}
}
