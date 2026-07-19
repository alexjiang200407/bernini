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
