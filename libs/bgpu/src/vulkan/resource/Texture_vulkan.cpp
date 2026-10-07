#include "resource/Texture_vulkan.h"
#include "convert_vulkan.h"
#include "resource/ImageMemory_vulkan.h"
#include "volk_vulkan.h"
#include <algorithm>
#include <bgpu/MemoryTag.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/FormatInfo.h>
#include <bgpu/types/TextureDimension.h>
#include <core/math.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <numeric>
#include <utility>

namespace bgpu
{
	Texture::Texture(
		core::SharedRef<ImageMemory> memory,
		TextureDesc                  desc,
		const bool                   tracked) noexcept :
		m_Desc(std::move(desc)), m_Memory(std::move(memory)), m_Image(m_Memory->GetVkImage()),
		m_Format(ConvertFormat(m_Desc.format)), m_Aspects(FormatAspects(m_Desc.format))
	{
		if (tracked)
			m_Tracked = TaggedBytes(MemoryTag::kDeviceTexture, m_Memory->GetAllocationSize());
	}

	Texture::Texture(const VkImage borrowed, TextureDesc desc) noexcept :
		m_Desc(std::move(desc)), m_Image(borrowed), m_Format(ConvertFormat(m_Desc.format)),
		m_Aspects(FormatAspects(m_Desc.format))
	{}

	VkImageSubresourceRange
	Texture::GetWholeRange() const noexcept
	{
		auto range       = VkImageSubresourceRange();
		range.aspectMask = m_Aspects;
		range.levelCount = VK_REMAINING_MIP_LEVELS;
		range.layerCount = VK_REMAINING_ARRAY_LAYERS;
		return range;
	}

	VkImageAspectFlags
	Texture::GetCopyAspect() const noexcept
	{
		return (m_Aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 ? VK_IMAGE_ASPECT_DEPTH_BIT : m_Aspects;
	}

	uint32_t
	Texture::GetCopyBlockBytes() const noexcept
	{
		// A depth aspect copies as its own texels, without the stencil a combined format packs.
		if ((m_Aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
			return m_Desc.format == Format::D16 ? 2U : 4U;
		return GetFormatInfo(m_Desc.format).bytesPerBlock;
	}

	TextureReadbackLayout
	Texture::GetReadbackLayout() const noexcept
	{
		constexpr uint64_t c_RowAlignment = 256;

		const uint32_t   blockEdge  = GetFormatInfo(m_Desc.format).blockEdgeTexels;
		const uint64_t   blockBytes = GetCopyBlockBytes();
		const VkExtent3D extent     = GetMipExtent(0);

		auto layout         = TextureReadbackLayout();
		layout.rowSizeBytes = core::div_ceil(extent.width, blockEdge) * blockBytes;
		layout.rowPitch = core::round_up(layout.rowSizeBytes, std::lcm(c_RowAlignment, blockBytes));
		layout.rowCount = core::div_ceil(extent.height, blockEdge);
		const uint64_t rows = static_cast<uint64_t>(layout.rowCount) * extent.depth;
		layout.totalBytes   = layout.rowPitch * (rows - 1) + layout.rowSizeBytes;
		return layout;
	}

	VkExtent3D
	Texture::GetMipExtent(const uint32_t mip) const noexcept
	{
		const bool volume = m_Desc.dimension == TextureDimension::kTexture3D;
		return VkExtent3D{
			.width  = std::max(m_Desc.width >> mip, 1U),
			.height = std::max(m_Desc.height >> mip, 1U),
			.depth  = volume ? std::max(m_Desc.depth >> mip, 1U) : 1U,
		};
	}
}
