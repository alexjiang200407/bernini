#include "cmd/CommandQueue_metal.h"

namespace bgl
{
	CommandQueue::CommandQueue(MTL::Device* device) :
		m_Queue(NS::TransferPtr(device->newCommandQueue())),
		m_Event(NS::TransferPtr(device->newSharedEvent()))
	{}

	uint64_t
	CommandQueue::ExecuteCommandList(ICommandList*) noexcept
	{
		gfatal("Metal backend: ExecuteCommandList not implemented yet");
		return 0;
	}

	bool
	CommandQueue::IsFenceComplete(uint64_t fenceValue) noexcept
	{
		return m_Event->signaledValue() >= fenceValue;
	}

	uint64_t
	CommandQueue::PollCurrentFenceValue() noexcept
	{
		return m_Event->signaledValue();
	}

	uint64_t
	CommandQueue::GetLastCompletedFence() const noexcept
	{
		return m_Event->signaledValue();
	}

	void
	CommandQueue::WaitForFenceCPUBlocking(uint64_t fenceValue) noexcept
	{
		if (m_Event->signaledValue() >= fenceValue)
		{
			return;
		}
		m_Event->waitUntilSignaledValue(fenceValue, /*timeoutMs*/ 5000);
	}

	// Cross-queue GPU synchronization is unused while there is a single queue; these land with the
	// second queue.
	void
	CommandQueue::InsertWait(uint64_t) noexcept
	{
		gfatal("Metal backend: InsertWait not implemented yet");
	}

	void
	CommandQueue::InsertWaitForQueueFence(ICommandQueue*, uint64_t) const noexcept
	{
		gfatal("Metal backend: InsertWaitForQueueFence not implemented yet");
	}

	void
	CommandQueue::InsertWaitForQueue(ICommandQueue*) const noexcept
	{
		gfatal("Metal backend: InsertWaitForQueue not implemented yet");
	}
}
