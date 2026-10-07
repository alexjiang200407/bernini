#pragma once

#include "volk_vulkan.h"
#include <span>

namespace bgpu
{
	class GpuContext;

	struct VulkanHandles
	{
		VkInstance       instance       = VK_NULL_HANDLE;
		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
		VkDevice         device         = VK_NULL_HANDLE;
	};

	/**
	 * The Vulkan objects behind a context, for the backend's own objects. Borrowed: the context
	 * destroys all three.
	 *
	 * The device was created with every queue of every family, since a device's queues are fixed
	 * when it is made, and with the features the minimum requirements name and no others.
	 */
	[[nodiscard]] VulkanHandles
	GetVulkanHandles(const GpuContext& context) noexcept;

	/** The device's queue families, indexed by family; every one was created with all its queues. */
	[[nodiscard]] std::span<const VkQueueFamilyProperties>
	GetVulkanQueueFamilies(const GpuContext& context) noexcept;
}
