#pragma once

// The Vulkan API as this backend reaches it: volk's function pointers, which bgpu defines and loads
// with the context. Never <vulkan/vulkan.h>, whose prototypes name symbols nothing links.
#if defined(_WIN32)
#	define VK_USE_PLATFORM_WIN32_KHR  // volk forward-declares the Win32 types rather than include them
#endif
#include <volk.h>  // IWYU pragma: export
