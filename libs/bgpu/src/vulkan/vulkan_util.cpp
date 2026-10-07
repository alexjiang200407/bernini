#include "vulkan_util.h"
#include "volk_vulkan.h"
#include <core/err/util.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vulkan/vk_enum_string_helper.h>

namespace bgpu
{
	void
	EnsureVk(const VkResult result, const std::string_view call) noexcept
	{
		if (result != VK_SUCCESS)
			core::fatal("{} failed: {}", call, string_VkResult(result));
	}

	void
	SetVkDebugName(
		const VkDevice         device,
		const VkObjectType     type,
		const uint64_t         handle,
		const std::string_view name) noexcept
	{
		if (vkSetDebugUtilsObjectNameEXT == nullptr || name.empty())
			return;

		const auto terminated = std::string(name);

		auto info         = VkDebugUtilsObjectNameInfoEXT();
		info.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		info.objectType   = type;
		info.objectHandle = handle;
		info.pObjectName  = terminated.c_str();
		(void)vkSetDebugUtilsObjectNameEXT(device, &info);
	}

	uint32_t
	FindMemoryType(
		const VkPhysicalDevice      physicalDevice,
		const uint32_t              typeBits,
		const VkMemoryPropertyFlags required,
		const VkMemoryPropertyFlags preferred)
	{
		auto properties = VkPhysicalDeviceMemoryProperties();
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);

		const auto find = [&](const VkMemoryPropertyFlags flags) -> uint32_t {
			for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
			{
				if ((typeBits & (1U << i)) != 0 &&
				    (properties.memoryTypes[i].propertyFlags & flags) == flags)
					return i;
			}
			return UINT32_MAX;
		};

		if (const uint32_t best = find(required | preferred); best != UINT32_MAX)
			return best;
		if (const uint32_t fallback = find(required); fallback != UINT32_MAX)
			return fallback;

		core::throw_runtime_error(
			"no Vulkan memory type in 0x{:x} has the properties 0x{:x}",
			typeBits,
			required);
	}

	void
	NotOnVulkanYet(const std::string_view entryPoint) noexcept
	{
		core::fatal(
			"{} is not implemented on Vulkan yet: textures, render targets and the meshlet "
			"pipeline arrive with the graphics RHI",
			entryPoint);
	}
}
