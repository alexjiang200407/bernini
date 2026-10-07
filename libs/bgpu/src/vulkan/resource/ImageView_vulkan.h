#pragma once
#include "volk_vulkan.h"
#include <string_view>
#include <utility>

namespace bgpu
{
	/** A VkImageView, destroyed with this. Every texture view's handle onto its image. */
	class ImageView final
	{
	public:
		ImageView() = default;

		ImageView(
			VkDevice                       device,
			VkImage                        image,
			VkImageViewType                type,
			VkFormat                       format,
			const VkImageSubresourceRange& range,
			std::string_view               debugName) noexcept;

		~ImageView() noexcept;

		ImageView(const ImageView&) = delete;
		ImageView(ImageView&& other) noexcept :
			m_Device(std::exchange(other.m_Device, VK_NULL_HANDLE)),
			m_View(std::exchange(other.m_View, VK_NULL_HANDLE))
		{}
		ImageView&
		operator=(const ImageView&) = delete;
		ImageView&
		operator=(ImageView&& other) noexcept;

		[[nodiscard]] VkImageView
		GetVkImageView() const noexcept
		{
			return m_View;
		}

	private:
		VkDevice    m_Device = VK_NULL_HANDLE;
		VkImageView m_View   = VK_NULL_HANDLE;
	};
}
