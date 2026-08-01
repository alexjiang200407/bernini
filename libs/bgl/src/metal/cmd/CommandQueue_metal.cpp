#include "cmd/CommandQueue_metal.h"
#include "MetalErrorChecker.h"

#include "cmd/CommandList_metal.h"

namespace bgl
{
	namespace
	{
		const char*
		EncoderErrorStateName(const MTL::CommandEncoderErrorState state) noexcept
		{
			switch (state)
			{
			case MTL::CommandEncoderErrorStateCompleted:
				return "completed";
			case MTL::CommandEncoderErrorStateAffected:
				return "affected";
			case MTL::CommandEncoderErrorStatePending:
				return "pending";
			case MTL::CommandEncoderErrorStateFaulted:
				return "FAULTED";
			case MTL::CommandEncoderErrorStateUnknown:
				break;
			}
			return "unknown";
		}

		// Which encoder in the buffer faulted, and which merely never ran. Present only when the
		// buffer was created asking for it, and only for a fault that Metal could attribute -- so an
		// absent list is the normal case, not a second failure.
		void
		LogEncoderInfo(const NS::Error* error)
		{
			if (error == nullptr || error->userInfo() == nullptr)
				return;

			const auto* infos = static_cast<NS::Array*>(
				error->userInfo()->object(MTL::CommandBufferEncoderInfoErrorKey));
			if (infos == nullptr)
				return;

			for (NS::UInteger i = 0; i < infos->count(); ++i)
			{
				const auto* info = static_cast<MTL::CommandBufferEncoderInfo*>(infos->object(i));
				const NS::String* label = info->label();

				gerror(
					"  encoder '{}': {}",
					label != nullptr ? label->utf8String() : "unlabelled",
					EncoderErrorStateName(info->errorState()));
			}
		}

		// A command buffer that faults reports it here and nowhere else: the signal it carries is its
		// status, and the fence it encoded still advances, so an unchecked failure reads downstream as
		// a frame that simply drew nothing.
		void
		LogOnFailure(MTL::CommandBuffer* cmdBuffer)
		{
			cmdBuffer->addCompletedHandler([](MTL::CommandBuffer* completed) {
				if (completed->status() != MTL::CommandBufferStatusError)
					return;

				const NS::Error* error = completed->error();
				gerror("Metal command buffer failed: {}", GetErrorDescription(error));
				LogEncoderInfo(error);
			});
		}
	}

	MTL::CommandBuffer*
	CommandQueue::NewCommandBuffer() const noexcept
	{
#if defined(BERNINI_GPU_DEBUG)
		// Attribution costs the driver per-encoder bookkeeping, so it is asked for only where the
		// D3D12 backend would have its debug layer on.
		NS::SharedPtr<MTL::CommandBufferDescriptor> desc =
			NS::TransferPtr(MTL::CommandBufferDescriptor::alloc()->init());
		desc->setErrorOptions(MTL::CommandBufferErrorOptionEncoderExecutionStatus);

		auto* cmdBuffer = MetalCheck(m_Queue->commandBuffer(desc.get()), "command buffer");
#else
		auto* cmdBuffer = MetalCheck(m_Queue->commandBuffer(), "command buffer");
#endif

		LogOnFailure(cmdBuffer);
		return cmdBuffer;
	}

	CommandQueue::CommandQueue(MTL::Device* device) :
		m_Queue(NS::TransferPtr(MetalCheck(device->newCommandQueue(), "command queue"))),
		m_Event(NS::TransferPtr(MetalCheck(device->newSharedEvent(), "shared event")))
	{}

	uint64_t
	CommandQueue::ExecuteCommandList(ICommandList* commandList) noexcept
	{
		gassert(commandList != nullptr, "Command list is not initialized.");

		auto* cmdBuffer = commandList->As<CommandList>()->GetCommandBuffer();
		gassert(cmdBuffer != nullptr, "Command list was not opened before execution");

		cmdBuffer->encodeSignalEvent(m_Event.get(), m_NextFenceValue);
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
		auto* cmdBuffer = NewCommandBuffer();
		cmdBuffer->encodeSignalEvent(m_Event.get(), m_NextFenceValue);
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
