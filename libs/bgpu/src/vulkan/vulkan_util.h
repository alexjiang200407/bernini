#pragma once
#include "volk_vulkan.h"
#include <cstdint>
#include <string_view>

namespace bgpu
{
	/** Ends the process naming `call` and the result, unless `result` is VK_SUCCESS. */
	void
	EnsureVk(VkResult result, std::string_view call) noexcept;

	/**
	 * Names `handle` for the validation layer's messages. A no-op without the debug layer, whose
	 * instance extension is the only source of the call.
	 */
	void
	SetVkDebugName(
		VkDevice         device,
		VkObjectType     type,
		uint64_t         handle,
		std::string_view name) noexcept;

	/**
	 * The first memory type `typeBits` allows that has every `required` flag, preferring one that
	 * also has every `preferred` flag.
	 *
	 * @throws std::runtime_error when no allowed type has the required flags.
	 */
	[[nodiscard]] uint32_t
	FindMemoryType(
		VkPhysicalDevice      physicalDevice,
		uint32_t              typeBits,
		VkMemoryPropertyFlags required,
		VkMemoryPropertyFlags preferred);

	/** Ends the process for an RHI entry point the Vulkan backend has not reached yet. */
	[[noreturn]] void
	NotOnVulkanYet(std::string_view entryPoint) noexcept;
}
