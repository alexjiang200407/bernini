#include "types/vk_format.h"

namespace bgl
{
	Format
	FromVkFormat(assetlib::VkFormat vkFormat) noexcept
	{
		using assetlib::VkFormat;
		switch (vkFormat)
		{
		case VkFormat::R8_UNORM:
			return Format::R8_UNORM;
		case VkFormat::R8G8_UNORM:
			return Format::RG8_UNORM;
		case VkFormat::R16G16_UNORM:
			return Format::RG16_UNORM;
		case VkFormat::R8G8B8A8_UNORM:
			return Format::RGBA8_UNORM;
		case VkFormat::R8G8B8A8_SRGB:
			return Format::SRGBA8_UNORM;
		case VkFormat::B8G8R8A8_UNORM:
			return Format::BGRA8_UNORM;
		case VkFormat::B8G8R8A8_SRGB:
			return Format::SBGRA8_UNORM;
		case VkFormat::R16G16_SFLOAT:
			return Format::RG16_FLOAT;
		case VkFormat::R16G16B16A16_UNORM:
			return Format::RGBA16_UNORM;
		case VkFormat::R16G16B16A16_SFLOAT:
			return Format::RGBA16_FLOAT;
		case VkFormat::R32_SFLOAT:
			return Format::R32_FLOAT;
		case VkFormat::R32G32_SFLOAT:
			return Format::RG32_FLOAT;
		case VkFormat::R32G32B32A32_SFLOAT:
			return Format::RGBA32_FLOAT;

		// BC1 covers both Vulkan's BC1_RGB and BC1_RGBA.
		case VkFormat::BC1_RGB_UNORM_BLOCK:
		case VkFormat::BC1_RGBA_UNORM_BLOCK:
			return Format::BC1_UNORM;
		case VkFormat::BC1_RGB_SRGB_BLOCK:
		case VkFormat::BC1_RGBA_SRGB_BLOCK:
			return Format::BC1_UNORM_SRGB;
		case VkFormat::BC2_UNORM_BLOCK:
			return Format::BC2_UNORM;
		case VkFormat::BC2_SRGB_BLOCK:
			return Format::BC2_UNORM_SRGB;
		case VkFormat::BC3_UNORM_BLOCK:
			return Format::BC3_UNORM;
		case VkFormat::BC3_SRGB_BLOCK:
			return Format::BC3_UNORM_SRGB;
		case VkFormat::BC4_UNORM_BLOCK:
			return Format::BC4_UNORM;
		case VkFormat::BC4_SNORM_BLOCK:
			return Format::BC4_SNORM;
		case VkFormat::BC5_UNORM_BLOCK:
			return Format::BC5_UNORM;
		case VkFormat::BC5_SNORM_BLOCK:
			return Format::BC5_SNORM;
		case VkFormat::BC6H_UFLOAT_BLOCK:
			return Format::BC6H_UFLOAT;
		case VkFormat::BC6H_SFLOAT_BLOCK:
			return Format::BC6H_SFLOAT;
		case VkFormat::BC7_UNORM_BLOCK:
			return Format::BC7_UNORM;
		case VkFormat::BC7_SRGB_BLOCK:
			return Format::BC7_UNORM_SRGB;

		case VkFormat::Undefined:
		default:
			gfatal("FromVkFormat unsupported format: {}", static_cast<uint32_t>(vkFormat));
		}
	}
}
