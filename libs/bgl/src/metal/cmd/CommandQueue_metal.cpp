#include "cmd/CommandQueue_metal.h"

#include "cmd/CommandList_metal.h"

namespace bgl
{
	namespace
	{
		// A command buffer that faults reports it here and nowhere else: the signal it carries is its
		// status, and the fence it encoded still advances, so an unchecked failure reads downstream as
		// a frame that simply drew nothing.
		void
		LogOnFailure(MTL::CommandBuffer* cmdBuffer)
		{
			cmdBuffer->addCompletedHandler([](MTL::CommandBuffer* completed) {
				if (completed->status() != MTL::CommandBufferStatusError)
					return;

				NS::Error* error = completed->error();
				gerror(
					"Metal command buffer failed: {}",
					error != nullptr ? error->localizedDescription()->utf8String() : "no error");
			});
		}
	}

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
		LogOnFailure(cmdBuffer);
		cmdBuffer->commit();

		if (m_ListsBuilding > 0)
			--m_ListsBuilding;

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
		LogOnFailure(cmdBuffer);
		cmdBuffer->commit();
		WaitForFenceCPUBlocking(m_NextFenceValue++);
	}

	namespace
	{
		constexpr const char* c_WaitTooLate =
			"Insert a GPU wait before opening the list that must observe it: {} list(s) are "
			"already "
			"building on this queue, and a wait cannot reach a command buffer already started";
	}

	void
	CommandQueue::InsertWait(uint64_t fenceValue) noexcept
	{
		gassert(m_ListsBuilding == 0, c_WaitTooLate, m_ListsBuilding);
		m_PendingWaits.push_back({ m_Event, fenceValue });
	}

	void
	CommandQueue::InsertWaitForQueueFence(ICommandQueue* cq, uint64_t fenceValue) const noexcept
	{
		gassert(cq != nullptr, "InsertWaitForQueueFence requires a non-null queue");
		gassert(m_ListsBuilding == 0, c_WaitTooLate, m_ListsBuilding);
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
	CommandQueue::BeginCommandBuffer(MTL::CommandBuffer* cmdBuffer) noexcept
	{
		for (const PendingWait& wait : m_PendingWaits)
			cmdBuffer->encodeWait(wait.event.get(), wait.fenceValue);
		m_PendingWaits.clear();
		++m_ListsBuilding;
	}
}
