#include "cmd/CommandQueue_metal.h"
#include "MetalErrorChecker.h"

#include "cmd/CommandList.h"
#include "cmd/CommandList_metal.h"
#include "cmd/CommandQueue.h"
#include <bgl_common/gassert.h>
#include <cstdint>

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

		auto* cmdBuffer = m_Queue->commandBuffer(desc.get());
#else
		auto* cmdBuffer = m_Queue->commandBuffer();
#endif

		gassert(cmdBuffer != nullptr, "Metal command buffer creation failed");

		LogOnFailure(cmdBuffer);
		return cmdBuffer;
	}

	CommandQueue::CommandQueue(MTL::Device* device) :
		m_Queue(NS::TransferPtr(device->newCommandQueue())),
		m_Event(NS::TransferPtr(device->newSharedEvent()))
	{
		gassert(m_Queue.get() != nullptr, "Metal command queue creation failed");
		gassert(m_Event.get() != nullptr, "Metal shared event creation failed");

		device->sampleTimestamps(&m_CpuBase, &m_GpuBase);
	}

	double
	CommandQueue::GetTimestampFrequency() const noexcept
	{
		if (m_TimestampFrequency != 0.0)
		{
			return m_TimestampFrequency;
		}

		// The CPU stamp is nanoseconds; the GPU stamp's unit is what is being measured. Two samples
		// a few milliseconds apart put the rate within a tenth of a percent, so a caller that asks
		// before that much has passed since construction waits the remainder out rather than
		// receiving a rate it would have to discard.
		constexpr MTL::Timestamp c_MinSpanNs = 5'000'000;

		MTL::Device*   device = m_Queue->device();
		MTL::Timestamp cpuNow = 0;
		MTL::Timestamp gpuNow = 0;
		do
		{
			device->sampleTimestamps(&cpuNow, &gpuNow);
		} while (cpuNow - m_CpuBase < c_MinSpanNs);

		if (gpuNow <= m_GpuBase)
		{
			return 0.0;
		}

		m_TimestampFrequency = static_cast<double>(gpuNow - m_GpuBase) * 1.0e9 /
		                       static_cast<double>(cpuNow - m_CpuBase);
		return m_TimestampFrequency;
	}

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
		// NewCommandBuffer autoreleases, so without a pool of its own the buffer would sit in
		// whichever one encloses the call -- the pool Graphics holds, which is the whole process.
		// Committed before the drain, so the driver owns it for the rest of its flight.
		NS::SharedPtr<NS::AutoreleasePool> pool =
			NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		// An empty command buffer signals past everything already committed: Metal keeps submission
		// order on a queue, so it cannot run before them.
		auto* cmdBuffer = NewCommandBuffer();
		cmdBuffer->encodeSignalEvent(m_Event.get(), m_NextFenceValue);
		cmdBuffer->commit();
		WaitForFenceCPUBlocking(m_NextFenceValue++);

		// The event fires as the GPU passes the signal, which leaves the driver still retiring the
		// buffer and releasing what it held -- and every caller frees resources with deferred=false
		// immediately after, which is only safe once it has. Metal keeps submission order, so this
		// buffer being retired puts every one committed before it behind us too.
		cmdBuffer->waitUntilCompleted();
	}

	void
	CommandQueue::InsertWait(uint64_t fenceValue) noexcept
	{
		gassert(
			m_ListsBuilding == 0,
			"Insert a GPU wait before opening the list that must observe it: {} list(s) are "
			"already building on this queue, and a wait cannot reach a command buffer already "
			"started",
			m_ListsBuilding);
		m_PendingWaits.push_back({ m_Event, fenceValue });
	}

	void
	CommandQueue::InsertWaitForQueueFence(ICommandQueue* cq, uint64_t fenceValue) const noexcept
	{
		gassert(cq != nullptr, "InsertWaitForQueueFence requires a non-null queue");
		gassert(
			m_ListsBuilding == 0,
			"Insert a GPU wait before opening the list that must observe it: {} list(s) are "
			"already building on this queue, and a wait cannot reach a command buffer already "
			"started",
			m_ListsBuilding);
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
