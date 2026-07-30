#include "cmd/CommandQueue_metal.h"

#include "cmd/CommandList_metal.h"

namespace bgl
{
	CommandQueue::CommandQueue(MTL::Device* device) :
		m_Queue(NS::TransferPtr(device->newCommandQueue())),
		m_Event(NS::TransferPtr(device->newSharedEvent()))
	{}

	uint64_t
	CommandQueue::ExecuteCommandList(ICommandList* commandList) noexcept
	{
		gassert(commandList != nullptr, "Command list is not initialized.");

		auto* cmdBuffer = commandList->As<CommandList>()->GetCommandBuffer();
		gassert(cmdBuffer != nullptr, "Command list was not opened before execution");

		cmdBuffer->encodeSignalEvent(m_Event.get(), m_NextFenceValue);
		cmdBuffer->commit();

		return m_NextFenceValue++;
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
		// Block until the fence actually reaches fenceValue -- the d3d12 counterpart waits INFINITE.
		// MTL::SharedEvent's CPU wait is bounded, so loop: it returns on signal or timeout, and a
		// timeout must not be mistaken for completion (that would let a caller read GPU work still in
		// flight). A real hang blocks here, exactly as an INFINITE wait would.
		while (m_Event->signaledValue() < fenceValue)
		{
			m_Event->waitUntilSignaledValue(fenceValue, /*timeoutMs*/ 5000);
		}
	}

	void
	CommandQueue::Flush() noexcept
	{
		// An empty command buffer signals past everything already committed: Metal keeps submission
		// order on a queue, so it cannot run before them.
		auto* cmdBuffer = m_Queue->commandBuffer();
		cmdBuffer->encodeSignalEvent(m_Event.get(), m_NextFenceValue);
		cmdBuffer->commit();
		WaitForFenceCPUBlocking(m_NextFenceValue++);
	}

	void
	CommandQueue::InsertWait(uint64_t fenceValue) noexcept
	{
		m_PendingWaits.push_back({ m_Event, fenceValue });
	}

	void
	CommandQueue::InsertWaitForQueueFence(ICommandQueue* cq, uint64_t fenceValue) const noexcept
	{
		gassert(cq != nullptr, "InsertWaitForQueueFence requires a non-null queue");
		m_PendingWaits.push_back({ cq->As<CommandQueue>()->m_Event, fenceValue });
	}

	void
	CommandQueue::InsertWaitForQueue(ICommandQueue* otherQueue) const noexcept
	{
		gassert(otherQueue != nullptr, "InsertWaitForQueue requires a non-null queue");
		// Everything submitted so far: the next value has not been signalled yet.
		InsertWaitForQueueFence(otherQueue, otherQueue->GetNextFenceValue() - 1);
	}

	void
	CommandQueue::EncodePendingWaits(MTL::CommandBuffer* cmdBuffer) const noexcept
	{
		for (const PendingWait& wait : m_PendingWaits)
			cmdBuffer->encodeWait(wait.event.get(), wait.value);
		m_PendingWaits.clear();
	}
}
