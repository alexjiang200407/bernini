#pragma once
#include <bgpu/cmd/CommandQueue.h>
#include <cstdint>

namespace bgpu
{
	/**
	 * A place on one queue's timeline: the work that queue had submitted when `value` was
	 * signalled. What one owner hands another so the second can wait for the first on the GPU
	 * (ICommandQueue::InsertWaitForQueueFence), never on the CPU.
	 *
	 * Holds a reference to the queue: whoever keeps a point keeps that queue alive.
	 */
	struct QueuePoint
	{
		CommandQueueRef queue;
		uint64_t        value = 0;

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return !queue.IsInitialized();
		}
	};
}
