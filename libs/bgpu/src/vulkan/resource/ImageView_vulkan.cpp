#include "resource/ImageView_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <cstdint>
#include <string_view>
#include <utility>

namespace bgpu
{
	ImageView::ImageView(
		const VkDevice                 device,
		const VkImage                  image,
		const VkImageViewType          type,
		const VkFormat                 format,
		const VkImageSubresourceRange& range,
		const std::string_view         debugName) noexcept : m_Device(device)
	{
		auto info             = VkImageViewCreateInfo();
		info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image            = image;
		info.viewType         = type;
		info.format           = format;
		info.subresourceRange = range;
		EnsureVk(vkCreateImageView(m_Device, &info, nullptr, &m_View), "vkCreateImageView");
		SetVkDebugName(
			m_Device,
			VK_OBJECT_TYPE_IMAGE_VIEW,
			reinterpret_cast<uint64_t>(m_View),
			debugName);
	}

	ImageView::~ImageView() noexcept
	{
		if (m_View != VK_NULL_HANDLE)
			vkDestroyImageView(m_Device, m_View, nullptr);
	}

	ImageView&
	ImageView::operator=(ImageView&& other) noexcept
	{
		if (this != &other)
		{
			if (m_View != VK_NULL_HANDLE)
				vkDestroyImageView(m_Device, m_View, nullptr);
			m_Device = std::exchange(other.m_Device, VK_NULL_HANDLE);
			m_View   = std::exchange(other.m_View, VK_NULL_HANDLE);
		}
		return *this;
	}
}
