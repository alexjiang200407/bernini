#include "convert_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/resource/Sampler.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/BlendState.h>
#include <bgpu/types/Color.h>
#include <bgpu/types/DepthStencilState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/types/RasterState.h>
#include <bgpu/types/TextureDimension.h>
#include <core/err/util.h>
#include <cstdint>
#include <span>
#include <vector>

namespace bgpu
{
	VkFormat
	ConvertFormat(const Format format) noexcept
	{
		switch (format)
		{
		case Format::UNKNOWN:
			return VK_FORMAT_UNDEFINED;
		case Format::R8_UINT:
			return VK_FORMAT_R8_UINT;
		case Format::R8_SINT:
			return VK_FORMAT_R8_SINT;
		case Format::R8_UNORM:
			return VK_FORMAT_R8_UNORM;
		case Format::R8_SNORM:
			return VK_FORMAT_R8_SNORM;
		case Format::RG8_UINT:
			return VK_FORMAT_R8G8_UINT;
		case Format::RG8_SINT:
			return VK_FORMAT_R8G8_SINT;
		case Format::RG8_UNORM:
			return VK_FORMAT_R8G8_UNORM;
		case Format::RG8_SNORM:
			return VK_FORMAT_R8G8_SNORM;
		case Format::R16_UINT:
			return VK_FORMAT_R16_UINT;
		case Format::R16_SINT:
			return VK_FORMAT_R16_SINT;
		case Format::R16_UNORM:
			return VK_FORMAT_R16_UNORM;
		case Format::R16_SNORM:
			return VK_FORMAT_R16_SNORM;
		case Format::R16_FLOAT:
			return VK_FORMAT_R16_SFLOAT;
		// DXGI names packed channels from the low bits up, Vulkan from the high bits down.
		case Format::BGRA4_UNORM:
			return VK_FORMAT_A4R4G4B4_UNORM_PACK16;
		case Format::B5G6R5_UNORM:
			return VK_FORMAT_R5G6B5_UNORM_PACK16;
		case Format::B5G5R5A1_UNORM:
			return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
		case Format::RGBA8_UINT:
			return VK_FORMAT_R8G8B8A8_UINT;
		case Format::RGBA8_SINT:
			return VK_FORMAT_R8G8B8A8_SINT;
		case Format::RGBA8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case Format::RGBA8_SNORM:
			return VK_FORMAT_R8G8B8A8_SNORM;
		case Format::BGRA8_UNORM:
		case Format::BGRX8_UNORM:
			return VK_FORMAT_B8G8R8A8_UNORM;
		case Format::SRGBA8_UNORM:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case Format::SBGRA8_UNORM:
		case Format::SBGRX8_UNORM:
			return VK_FORMAT_B8G8R8A8_SRGB;
		case Format::R10G10B10A2_UNORM:
			return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		case Format::R11G11B10_FLOAT:
			return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
		case Format::RGB9E5_FLOAT:
			return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
		case Format::RG16_UINT:
			return VK_FORMAT_R16G16_UINT;
		case Format::RG16_SINT:
			return VK_FORMAT_R16G16_SINT;
		case Format::RG16_UNORM:
			return VK_FORMAT_R16G16_UNORM;
		case Format::RG16_SNORM:
			return VK_FORMAT_R16G16_SNORM;
		case Format::RG16_FLOAT:
			return VK_FORMAT_R16G16_SFLOAT;
		case Format::R32_UINT:
			return VK_FORMAT_R32_UINT;
		case Format::R32_SINT:
			return VK_FORMAT_R32_SINT;
		case Format::R32_FLOAT:
			return VK_FORMAT_R32_SFLOAT;
		case Format::RGBA16_UINT:
			return VK_FORMAT_R16G16B16A16_UINT;
		case Format::RGBA16_SINT:
			return VK_FORMAT_R16G16B16A16_SINT;
		case Format::RGBA16_FLOAT:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case Format::RGBA16_UNORM:
			return VK_FORMAT_R16G16B16A16_UNORM;
		case Format::RGBA16_SNORM:
			return VK_FORMAT_R16G16B16A16_SNORM;
		case Format::RG32_UINT:
			return VK_FORMAT_R32G32_UINT;
		case Format::RG32_SINT:
			return VK_FORMAT_R32G32_SINT;
		case Format::RG32_FLOAT:
			return VK_FORMAT_R32G32_SFLOAT;
		case Format::RGB32_UINT:
			return VK_FORMAT_R32G32B32_UINT;
		case Format::RGB32_SINT:
			return VK_FORMAT_R32G32B32_SINT;
		case Format::RGB32_FLOAT:
			return VK_FORMAT_R32G32B32_SFLOAT;
		case Format::RGBA32_UINT:
			return VK_FORMAT_R32G32B32A32_UINT;
		case Format::RGBA32_SINT:
			return VK_FORMAT_R32G32B32A32_SINT;
		case Format::RGBA32_FLOAT:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		case Format::D16:
			return VK_FORMAT_D16_UNORM;
		case Format::D24S8:
		case Format::X24G8_UINT:
		case Format::D32S8:
		case Format::X32G8_UINT:
			return VK_FORMAT_D32_SFLOAT_S8_UINT;
		case Format::D32:
			return VK_FORMAT_D32_SFLOAT;
		case Format::BC1_UNORM:
			return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		case Format::BC1_UNORM_SRGB:
			return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
		case Format::BC2_UNORM:
			return VK_FORMAT_BC2_UNORM_BLOCK;
		case Format::BC2_UNORM_SRGB:
			return VK_FORMAT_BC2_SRGB_BLOCK;
		case Format::BC3_UNORM:
			return VK_FORMAT_BC3_UNORM_BLOCK;
		case Format::BC3_UNORM_SRGB:
			return VK_FORMAT_BC3_SRGB_BLOCK;
		case Format::BC4_UNORM:
			return VK_FORMAT_BC4_UNORM_BLOCK;
		case Format::BC4_SNORM:
			return VK_FORMAT_BC4_SNORM_BLOCK;
		case Format::BC5_UNORM:
			return VK_FORMAT_BC5_UNORM_BLOCK;
		case Format::BC5_SNORM:
			return VK_FORMAT_BC5_SNORM_BLOCK;
		case Format::BC6H_UFLOAT:
			return VK_FORMAT_BC6H_UFLOAT_BLOCK;
		case Format::BC6H_SFLOAT:
			return VK_FORMAT_BC6H_SFLOAT_BLOCK;
		case Format::BC7_UNORM:
			return VK_FORMAT_BC7_UNORM_BLOCK;
		case Format::BC7_UNORM_SRGB:
			return VK_FORMAT_BC7_SRGB_BLOCK;
		case Format::COUNT:
			break;
		}
		core::fatal("Unknown format {}", static_cast<uint32_t>(format));
	}

	VkImageAspectFlags
	FormatAspects(const Format format) noexcept
	{
		switch (format)
		{
		case Format::D16:
		case Format::D32:
			return VK_IMAGE_ASPECT_DEPTH_BIT;
		case Format::D24S8:
		case Format::X24G8_UINT:
		case Format::D32S8:
		case Format::X32G8_UINT:
			return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		default:
			return VK_IMAGE_ASPECT_COLOR_BIT;
		}
	}

	VkImageAspectFlags
	ViewAspect(const VkImageAspectFlags imageAspects, const Format viewFormat) noexcept
	{
		if ((imageAspects & VK_IMAGE_ASPECT_DEPTH_BIT) == 0)
			return VK_IMAGE_ASPECT_COLOR_BIT;
		if (viewFormat == Format::X24G8_UINT || viewFormat == Format::X32G8_UINT)
			return VK_IMAGE_ASPECT_STENCIL_BIT;
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	VkImageLayout
	ConvertImageLayout(const BarrierLayout layout) noexcept
	{
		switch (layout)
		{
		case BarrierLayout::kUndefined:
			return VK_IMAGE_LAYOUT_UNDEFINED;
		case BarrierLayout::kPresent:
			return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		case BarrierLayout::kCommon:
		case BarrierLayout::kGenericRead:
		case BarrierLayout::kShaderResource:
		case BarrierLayout::kUnorderedAccess:
		case BarrierLayout::kRenderTarget:
		case BarrierLayout::kDepthWrite:
		case BarrierLayout::kDepthRead:
		case BarrierLayout::kCopySource:
		case BarrierLayout::kCopyDest:
			return VK_IMAGE_LAYOUT_GENERAL;
		}
		core::fatal("Unknown barrier layout {}", static_cast<uint32_t>(layout));
	}

	VkImageViewType
	ConvertImageViewType(const TextureDimension dimension) noexcept
	{
		switch (dimension)
		{
		case TextureDimension::kTexture1D:
			return VK_IMAGE_VIEW_TYPE_1D;
		case TextureDimension::kTexture1DArray:
			return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
		case TextureDimension::kTexture2D:
		case TextureDimension::kTexture2DMS:
			return VK_IMAGE_VIEW_TYPE_2D;
		case TextureDimension::kTexture2DArray:
		case TextureDimension::kTexture2DMSArray:
			return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		case TextureDimension::kTextureCube:
			return VK_IMAGE_VIEW_TYPE_CUBE;
		case TextureDimension::kTextureCubeArray:
			return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
		case TextureDimension::kTexture3D:
			return VK_IMAGE_VIEW_TYPE_3D;
		case TextureDimension::kUnknown:
			break;
		}
		core::fatal("A texture view needs a dimension");
	}

	namespace
	{
		[[nodiscard]] VkSamplerAddressMode
		ConvertAddressMode(const SamplerAddressMode mode) noexcept
		{
			switch (mode)
			{
			case SamplerAddressMode::kClamp:
				return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			case SamplerAddressMode::kWrap:
				return VK_SAMPLER_ADDRESS_MODE_REPEAT;
			case SamplerAddressMode::kBorder:
				return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			case SamplerAddressMode::kMirror:
				return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			case SamplerAddressMode::kMirrorOnce:
				return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
			}
			core::fatal("Unknown sampler address mode {}", static_cast<uint32_t>(mode));
		}

		[[nodiscard]] VkBorderColor
		ConvertBorderColor(const Color& color) noexcept
		{
			if (color == Color(0.f, 0.f, 0.f, 0.f))
				return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			if (color == Color(0.f, 0.f, 0.f, 1.f))
				return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			if (color == Color(1.f, 1.f, 1.f, 1.f))
				return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			core::fatal(
				"A Vulkan sampler's border is transparent black, opaque black or opaque white, not "
				"({}, {}, {}, {})",
				color.r,
				color.g,
				color.b,
				color.a);
		}
	}

	VkSamplerCreateInfo
	ConvertSamplerDesc(
		const SamplerDesc&                desc,
		VkSamplerReductionModeCreateInfo& reduction) noexcept
	{
		auto info      = VkSamplerCreateInfo();
		info.sType     = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = desc.magFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		info.minFilter = desc.minFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		info.mipmapMode =
			desc.mipFilter ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = ConvertAddressMode(desc.addressU);
		info.addressModeV = ConvertAddressMode(desc.addressV);
		info.addressModeW = ConvertAddressMode(desc.addressW);
		info.mipLodBias   = desc.mipBias;
		info.minLod       = 0.f;
		info.maxLod       = VK_LOD_CLAMP_NONE;
		info.borderColor  = ConvertBorderColor(desc.borderColor);

		// D3D12's anisotropic filter is linear in every dimension, whatever the three flags say.
		if (desc.maxAnisotropy > 1.f)
		{
			info.anisotropyEnable = VK_TRUE;
			info.maxAnisotropy    = desc.maxAnisotropy;
			info.magFilter        = VK_FILTER_LINEAR;
			info.minFilter        = VK_FILTER_LINEAR;
			info.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		}

		switch (desc.reductionType)
		{
		case SamplerReductionType::kStandard:
			break;
		case SamplerReductionType::kComparison:
			info.compareEnable = VK_TRUE;
			info.compareOp     = VK_COMPARE_OP_LESS;
			break;
		case SamplerReductionType::kMinimum:
		case SamplerReductionType::kMaximum:
			reduction               = VkSamplerReductionModeCreateInfo();
			reduction.sType         = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO;
			reduction.reductionMode = desc.reductionType == SamplerReductionType::kMinimum ?
			                              VK_SAMPLER_REDUCTION_MODE_MIN :
			                              VK_SAMPLER_REDUCTION_MODE_MAX;
			info.pNext              = &reduction;
			break;
		}
		return info;
	}

	namespace
	{
		[[nodiscard]] VkCompareOp
		ConvertCompare(const ComparisonFunc func) noexcept
		{
			switch (func)
			{
			case ComparisonFunc::kNever:
				return VK_COMPARE_OP_NEVER;
			case ComparisonFunc::kLess:
				return VK_COMPARE_OP_LESS;
			case ComparisonFunc::kEqual:
				return VK_COMPARE_OP_EQUAL;
			case ComparisonFunc::kLessOrEqual:
				return VK_COMPARE_OP_LESS_OR_EQUAL;
			case ComparisonFunc::kGreater:
				return VK_COMPARE_OP_GREATER;
			case ComparisonFunc::kNotEqual:
				return VK_COMPARE_OP_NOT_EQUAL;
			case ComparisonFunc::kGreaterOrEqual:
				return VK_COMPARE_OP_GREATER_OR_EQUAL;
			case ComparisonFunc::kAlways:
				return VK_COMPARE_OP_ALWAYS;
			}
			core::fatal("Unknown comparison {}", static_cast<uint32_t>(func));
		}

		[[nodiscard]] VkStencilOp
		ConvertStencil(const StencilOp op) noexcept
		{
			switch (op)
			{
			case StencilOp::kKeep:
				return VK_STENCIL_OP_KEEP;
			case StencilOp::kZero:
				return VK_STENCIL_OP_ZERO;
			case StencilOp::kReplace:
				return VK_STENCIL_OP_REPLACE;
			case StencilOp::kIncrementAndClamp:
				return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
			case StencilOp::kDecrementAndClamp:
				return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
			case StencilOp::kInvert:
				return VK_STENCIL_OP_INVERT;
			case StencilOp::kIncrementAndWrap:
				return VK_STENCIL_OP_INCREMENT_AND_WRAP;
			case StencilOp::kDecrementAndWrap:
				return VK_STENCIL_OP_DECREMENT_AND_WRAP;
			}
			core::fatal("Unknown stencil op {}", static_cast<uint32_t>(op));
		}

		[[nodiscard]] VkStencilOpState
		ConvertStencilFace(
			const DepthStencilState&                state,
			const DepthStencilState::StencilOpDesc& face) noexcept
		{
			auto result        = VkStencilOpState();
			result.failOp      = ConvertStencil(face.failOp);
			result.passOp      = ConvertStencil(face.passOp);
			result.depthFailOp = ConvertStencil(face.depthFailOp);
			result.compareOp   = ConvertCompare(face.stencilFunc);
			result.compareMask = state.stencilReadMask;
			result.writeMask   = state.stencilWriteMask;
			result.reference   = state.stencilRefValue;
			return result;
		}

		[[nodiscard]] VkBlendFactor
		ConvertBlendFactor(const BlendFactor factor) noexcept
		{
			switch (factor)
			{
			case BlendFactor::kZero:
				return VK_BLEND_FACTOR_ZERO;
			case BlendFactor::kOne:
				return VK_BLEND_FACTOR_ONE;
			case BlendFactor::kSrcColor:
				return VK_BLEND_FACTOR_SRC_COLOR;
			case BlendFactor::kInvSrcColor:
				return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
			case BlendFactor::kSrcAlpha:
				return VK_BLEND_FACTOR_SRC_ALPHA;
			case BlendFactor::kInvSrcAlpha:
				return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			case BlendFactor::kDstAlpha:
				return VK_BLEND_FACTOR_DST_ALPHA;
			case BlendFactor::kInvDstAlpha:
				return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
			case BlendFactor::kDstColor:
				return VK_BLEND_FACTOR_DST_COLOR;
			case BlendFactor::kInvDstColor:
				return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
			case BlendFactor::kSrcAlphaSaturate:
				return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
			case BlendFactor::kConstantColor:
				return VK_BLEND_FACTOR_CONSTANT_COLOR;
			case BlendFactor::kInvConstantColor:
				return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
			case BlendFactor::kSrc1Color:
				return VK_BLEND_FACTOR_SRC1_COLOR;
			case BlendFactor::kInvSrc1Color:
				return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
			case BlendFactor::kSrc1Alpha:
				return VK_BLEND_FACTOR_SRC1_ALPHA;
			case BlendFactor::kInvSrc1Alpha:
				return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
			}
			core::fatal("Unknown blend factor {}", static_cast<uint32_t>(factor));
		}

		[[nodiscard]] VkBlendOp
		ConvertBlendOp(const BlendOp op) noexcept
		{
			switch (op)
			{
			case BlendOp::kAdd:
				return VK_BLEND_OP_ADD;
			case BlendOp::kSubtract:
				return VK_BLEND_OP_SUBTRACT;
			case BlendOp::kReverseSubtract:
				return VK_BLEND_OP_REVERSE_SUBTRACT;
			case BlendOp::kMin:
				return VK_BLEND_OP_MIN;
			case BlendOp::kMax:
				return VK_BLEND_OP_MAX;
			}
			core::fatal("Unknown blend op {}", static_cast<uint32_t>(op));
		}
	}

	VkPipelineRasterizationStateCreateInfo
	ConvertRasterState(const RasterState& state) noexcept
	{
		core::ensure(
			!state.conservativeRasterEnable,
			"Conservative rasterization is not part of the Vulkan bar");
		core::ensure(
			state.forcedSampleCount == 0,
			"A forced sample count is not part of the Vulkan bar");

		auto info             = VkPipelineRasterizationStateCreateInfo();
		info.sType            = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		info.depthClampEnable = state.depthClipEnable ? VK_FALSE : VK_TRUE;
		info.polygonMode = state.fillMode == RasterFillMode::kWireframe ? VK_POLYGON_MODE_LINE :
		                                                                  VK_POLYGON_MODE_FILL;
		switch (state.cullMode)
		{
		case RasterCullMode::kBack:
			info.cullMode = VK_CULL_MODE_BACK_BIT;
			break;
		case RasterCullMode::kFront:
			info.cullMode = VK_CULL_MODE_FRONT_BIT;
			break;
		case RasterCullMode::kNone:
			info.cullMode = VK_CULL_MODE_NONE;
			break;
		}
		// The viewport's negative height mirrors D3D's clip space into Vulkan's, winding included,
		// so D3D12's rule carries over unchanged.
		info.frontFace =
			state.frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
		info.depthBiasEnable =
			state.depthBias != 0 || state.slopeScaledDepthBias != 0.f ? VK_TRUE : VK_FALSE;
		info.depthBiasConstantFactor = static_cast<float>(state.depthBias);
		info.depthBiasClamp          = state.depthBiasClamp;
		info.depthBiasSlopeFactor    = state.slopeScaledDepthBias;
		info.lineWidth               = 1.f;
		return info;
	}

	VkPipelineDepthStencilStateCreateInfo
	ConvertDepthStencilState(const DepthStencilState& state) noexcept
	{
		auto info              = VkPipelineDepthStencilStateCreateInfo();
		info.sType             = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		info.depthTestEnable   = state.depthTestEnable ? VK_TRUE : VK_FALSE;
		info.depthWriteEnable  = state.depthWriteEnable ? VK_TRUE : VK_FALSE;
		info.depthCompareOp    = ConvertCompare(state.depthFunc);
		info.stencilTestEnable = state.stencilEnable ? VK_TRUE : VK_FALSE;
		info.front             = ConvertStencilFace(state, state.frontFaceStencil);
		info.back              = ConvertStencilFace(state, state.backFaceStencil);
		info.minDepthBounds    = 0.f;
		info.maxDepthBounds    = 1.f;
		return info;
	}

	VkPipelineColorBlendAttachmentState
	ConvertBlendTarget(const BlendState::RenderTarget& target) noexcept
	{
		auto result                = VkPipelineColorBlendAttachmentState();
		result.blendEnable         = target.blendEnable ? VK_TRUE : VK_FALSE;
		result.srcColorBlendFactor = ConvertBlendFactor(target.srcBlend);
		result.dstColorBlendFactor = ConvertBlendFactor(target.destBlend);
		result.colorBlendOp        = ConvertBlendOp(target.blendOp);
		result.srcAlphaBlendFactor = ConvertBlendFactor(target.srcBlendAlpha);
		result.dstAlphaBlendFactor = ConvertBlendFactor(target.destBlendAlpha);
		result.alphaBlendOp        = ConvertBlendOp(target.blendOpAlpha);
		// Both name red, green, blue and alpha as bits 0 to 3.
		result.colorWriteMask = static_cast<VkColorComponentFlags>(target.colorWriteMask);
		return result;
	}

	VkPipelineStageFlags2
	ConvertBarrierSync(const BarrierSync sync) noexcept
	{
		core::ensure(
			!(sync & BarrierSyncFlag::kRayTracing),
			"Ray tracing is not part of the Vulkan bar");

		VkPipelineStageFlags2 result = VK_PIPELINE_STAGE_2_NONE;
		if (sync & BarrierSyncFlag::kAllCommands)
			result |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		if (sync & BarrierSyncFlag::kCopy)
			result |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
		if (sync & BarrierSyncFlag::kResolve)
			result |= VK_PIPELINE_STAGE_2_RESOLVE_BIT;
		if (sync & BarrierSyncFlag::kInputAssembler)
			result |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
		// D3D12's vertex shading is every stage before the rasterizer, the mesh stages included.
		if (sync & BarrierSyncFlag::kVertexShader)
			result |= VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT;
		if (sync & BarrierSyncFlag::kPixelShader)
			result |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		if (sync & BarrierSyncFlag::kComputeShader)
			result |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		if (sync & BarrierSyncFlag::kRenderTarget)
			result |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		if (sync & BarrierSyncFlag::kDepthStencil)
		{
			result |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
			          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		}
		if (sync & BarrierSyncFlag::kIndirectArgument)
			result |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
		return result;
	}

	VkAccessFlags2
	ConvertBarrierAccess(const BarrierAccess access) noexcept
	{
		core::ensure(
			!(access & BarrierAccessFlag::kAccelStructRead) &&
				!(access & BarrierAccessFlag::kAccelStructWrite),
			"Ray tracing is not part of the Vulkan bar");

		VkAccessFlags2 result = VK_ACCESS_2_NONE;
		if (access & BarrierAccessFlag::kCommon)
			result |= VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		if (access & BarrierAccessFlag::kIndexBuffer)
			result |= VK_ACCESS_2_INDEX_READ_BIT;
		if (access & BarrierAccessFlag::kVertexBuffer)
			result |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
		if (access & BarrierAccessFlag::kConstantBuffer)
			result |= VK_ACCESS_2_UNIFORM_READ_BIT;
		if (access & BarrierAccessFlag::kShaderResource)
			result |= VK_ACCESS_2_SHADER_READ_BIT;
		if (access & BarrierAccessFlag::kUnorderedAccess)
			result |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
		if (access & BarrierAccessFlag::kRenderTarget)
		{
			result |=
				VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		}
		if (access & BarrierAccessFlag::kDepthWrite)
		{
			result |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}
		if (access & BarrierAccessFlag::kDepthRead)
			result |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		if (access & BarrierAccessFlag::kIndirectArgument)
			result |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
		if (access & BarrierAccessFlag::kCopySource)
			result |= VK_ACCESS_2_TRANSFER_READ_BIT;
		if (access & BarrierAccessFlag::kCopyDest)
			result |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
		return result;
	}

	std::vector<uint32_t>
	QueueFamiliesFor(const QueueType type, const std::span<const VkQueueFamilyProperties> families)
	{
		enum class FamilyKind : uint8_t
		{
			kNone,
			kTransferOnly,
			kComputeOnly,
			kGraphicsCompute,
		};

		// Graphics and compute families support transfers whether or not they say so.
		const auto kindOf = [](const VkQueueFlags flags) {
			const bool graphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
			const bool compute  = (flags & VK_QUEUE_COMPUTE_BIT) != 0;
			if (graphics && compute)
				return FamilyKind::kGraphicsCompute;
			if (compute)
				return FamilyKind::kComputeOnly;
			if ((flags & VK_QUEUE_TRANSFER_BIT) != 0)
				return FamilyKind::kTransferOnly;
			return FamilyKind::kNone;
		};

		auto order = std::vector<FamilyKind>();
		switch (type)
		{
		case QueueType::kGraphics:
			order = { FamilyKind::kGraphicsCompute };
			break;
		case QueueType::kCompute:
			order = { FamilyKind::kComputeOnly, FamilyKind::kGraphicsCompute };
			break;
		case QueueType::kCopy:
			order = { FamilyKind::kTransferOnly,
				      FamilyKind::kComputeOnly,
				      FamilyKind::kGraphicsCompute };
			break;
		}

		auto result = std::vector<uint32_t>();
		for (const FamilyKind wanted : order)
		{
			for (uint32_t i = 0; i < families.size(); ++i)
			{
				if (families[i].queueCount > 0 && kindOf(families[i].queueFlags) == wanted)
					result.push_back(i);
			}
		}
		core::ensure(!result.empty(), "The Vulkan device has no queue family for this queue type");
		return result;
	}
}
