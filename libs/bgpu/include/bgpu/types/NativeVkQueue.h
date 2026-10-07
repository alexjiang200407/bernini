#pragma once
#include <cstdint>
#include <mutex>

namespace bgpu
{
	/**
	 * What a Vulkan `ICommandQueue` answers for `NativeObjectType::kVkQueue`: enough for a caller to
	 * submit beside the queue on the same `VkQueue` -- a present, which no RHI submission can make.
	 *
	 * `timeline` is the queue's fence: it reaches a value when the `ExecuteCommandList` that returned
	 * that value completes. Every submission to `queue`, a caller's included, holds `submitLock`,
	 * which every owner of that `VkQueue` shares. Borrowed, for the queue's lifetime.
	 */
	struct NativeVkQueue
	{
		void*       queue      = nullptr;  // VkQueue
		uint32_t    family     = 0;
		void*       timeline   = nullptr;  // VkSemaphore, of type VK_SEMAPHORE_TYPE_TIMELINE
		std::mutex* submitLock = nullptr;
	};
}
