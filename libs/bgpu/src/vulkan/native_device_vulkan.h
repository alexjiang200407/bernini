#pragma once

#include "volk_vulkan.h"
#include <cstdint>
#include <mutex>
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

	/**
	 * One of the device's queues, taken by an owner. Vulkan requires a queue's submissions to be
	 * externally synchronized, and two owners may hold one queue, so every submit holds `submitLock`.
	 */
	struct VulkanQueue
	{
		VkQueue     queue      = VK_NULL_HANDLE;
		uint32_t    family     = 0;
		std::mutex* submitLock = nullptr;
	};

	/**
	 * The queue the fewest owners hold among every queue of `families`, earlier families and queues
	 * first among equals: a queue is shared only when every one of every family is taken. Released
	 * with ReleaseVulkanQueue.
	 *
	 * @pre `families` is not empty and names families of this context's device.
	 */
	[[nodiscard]] VulkanQueue
	AcquireVulkanQueue(const GpuContext& context, std::span<const uint32_t> families) noexcept;

	void
	ReleaseVulkanQueue(const GpuContext& context, const VulkanQueue& queue) noexcept;
}
