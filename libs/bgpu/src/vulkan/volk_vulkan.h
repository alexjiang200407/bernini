#pragma once

// The Vulkan API as this backend reaches it: volk's function pointers over the Khronos headers.
// Never <vulkan/vulkan.h>, whose prototypes name symbols nothing here links. VolkImpl.cpp defines
// the pointers, once per process.
#include <volk.h>  // IWYU pragma: export
