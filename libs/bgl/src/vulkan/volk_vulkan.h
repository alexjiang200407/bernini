#pragma once

// The Vulkan API as this backend reaches it: volk's function pointers, bgl's own copy in namespace
// volk (VOLK_NAMESPACE, set on the target) so it is reachable whether bgl is a DLL beside bgpu or
// linked into one binary with it. Never <vulkan/vulkan.h>, whose prototypes name symbols nothing links.
#if defined(_WIN32)
#	define VK_USE_PLATFORM_WIN32_KHR  // volk forward-declares the Win32 types rather than include them
#endif
#include <volk.h>  // IWYU pragma: export

namespace bgl
{
	/**
	 * Points bgl's volk at the device bgpu created, process-wide, as bgpu's own are: called for every
	 * swapchain, since the device may not be the last one's. Only on the thread that drives
	 * IGraphics, which is every caller of the pointers too.
	 *
	 * @throws GraphicsError if the Vulkan loader cannot be loaded.
	 */
	void
	LoadVulkanFunctions(VkInstance instance, VkDevice device);
}
