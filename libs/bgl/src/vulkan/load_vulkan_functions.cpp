#include "volk_vulkan.h"
#include <bgl/IGraphics.h>
#include <mutex>

namespace bgl
{
	void
	LoadVulkanFunctions(const VkInstance instance, const VkDevice device)
	{
		static auto g_Mutex       = std::mutex();
		static bool g_Initialized = false;

		const auto lock = std::scoped_lock(g_Mutex);
		if (!g_Initialized)
		{
			if (volkInitialize() != VK_SUCCESS)
			{
				throw GraphicsError("Vulkan backend: the Vulkan loader could not be loaded");
			}
			g_Initialized = true;
		}
		volkLoadInstanceOnly(instance);
		volkLoadDevice(device);
	}
}
