#pragma once
#include "resource/ImageMemory_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/MemoryTag.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/Texture.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <utility>

namespace bgpu
{
	/**
	 * A texture in a manager's pool: its image, the format and aspects Vulkan knows it by, and its
	 * desc. The image is held through its memory when a bgpu manager made it, and borrowed when one
	 * did not -- a swapchain's, kept alive by its maker while the handle lives.
	 */
	class Texture final
	{
	public:
		Texture() = default;

		/**
		 * @param tracked whether the bytes are charged here: a texture adopted from another owner
		 * stays charged to the owner that allocated it.
		 */
		Texture(core::SharedRef<ImageMemory> memory, TextureDesc desc, bool tracked) noexcept;

		Texture(VkImage borrowed, TextureDesc desc) noexcept;

		~Texture() noexcept = default;

		Texture(const Texture&)     = delete;
		Texture(Texture&&) noexcept = default;
		Texture&
		operator=(const Texture&) = delete;
		Texture&
		operator=(Texture&&) noexcept = default;

		[[nodiscard]] VkImage
		GetVkImage() const noexcept
		{
			return m_Image;
		}

		/** The memory behind an image a bgpu manager made; null for a borrowed one. */
		[[nodiscard]] const core::SharedRef<ImageMemory>&
		GetMemory() const noexcept
		{
			return m_Memory;
		}

		[[nodiscard]] const TextureDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]] VkFormat
		GetVkFormat() const noexcept
		{
			return m_Format;
		}

		[[nodiscard]] VkImageAspectFlags
		GetAspects() const noexcept
		{
			return m_Aspects;
		}

		/** Every mip and layer of every aspect. */
		[[nodiscard]] VkImageSubresourceRange
		GetWholeRange() const noexcept;

		/** Mip `mip`'s width, height and depth, never below one texel. */
		[[nodiscard]] VkExtent3D
		GetMipExtent(uint32_t mip) const noexcept;

		/** The one aspect a buffer copy moves: depth for a depth-stencil image, as D3D12's plane 0. */
		[[nodiscard]] VkImageAspectFlags
		GetCopyAspect() const noexcept;

		/** Bytes per texel block of the copy aspect, as a buffer holds them. */
		[[nodiscard]] uint32_t
		GetCopyBlockBytes() const noexcept;

		/**
		 * Mip 0 of the first layer as CopyTextureToReadback lays it out: rows padded to 256 bytes as
		 * D3D12's footprint is, rounded up to a whole number of blocks.
		 */
		[[nodiscard]] TextureReadbackLayout
		GetReadbackLayout() const noexcept;

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_Image == VK_NULL_HANDLE;
		}

	private:
		TextureDesc                  m_Desc;
		core::SharedRef<ImageMemory> m_Memory;
		VkImage                      m_Image   = VK_NULL_HANDLE;
		VkFormat                     m_Format  = VK_FORMAT_UNDEFINED;
		VkImageAspectFlags           m_Aspects = 0;
		TaggedBytes                  m_Tracked;
	};
}
