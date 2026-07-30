#pragma once
#include "metal_cpp.h"

#include "cmd/CommandQueue.h"

#include <core/ref/RefCounter.h>

namespace bgl
{
	/**
	 * The RHI command queue over an MTL::CommandQueue. The fence-value model is a monotonically
	 * increasing MTL::SharedEvent: a submission signals the next value, and CPU/GPU waits compare
	 * against the event's signalled value.
	 *
	 * **A GPU-side wait applies to the next command list opened on this queue, not to work already
	 * recorded.** D3D12 waits on the queue object, which gates everything submitted afterwards;
	 * Metal encodes the wait into a command buffer, and one encoded after that buffer's encoders
	 * would sit past the work it is meant to gate. So InsertWait* records the wait and
	 * CommandList::Open drains it onto the buffer it is opening. Insert before opening the list that
	 * must observe it.
	 */
	class CommandQueue final : public core::RefCounter<ICommandQueue>
	{
	public:
		explicit CommandQueue(MTL::Device* device);

		uint64_t
		ExecuteCommandList(ICommandList* commandList) noexcept override;

		[[nodiscard]] bool
		IsFenceComplete(uint64_t fenceValue) noexcept override;

		[[nodiscard]] uint64_t
		PollCurrentFenceValue() noexcept override;

		[[nodiscard]] uint64_t
		GetLastCompletedFence() const noexcept override;

		[[nodiscard]] uint64_t
		GetNextFenceValue() const noexcept override
		{
			return m_NextFenceValue;
		}

		void
		InsertWait(uint64_t fenceValue) noexcept override;

		void
		InsertWaitForQueueFence(ICommandQueue* cq, uint64_t fenceValue) const noexcept override;

		void
		InsertWaitForQueue(ICommandQueue* otherQueue) const noexcept override;

		void
		WaitForFenceCPUBlocking(uint64_t fenceValue) noexcept override;

		void
		Flush() noexcept override;

		[[nodiscard]] MTL::CommandQueue*
		GetMTLCommandQueue() const noexcept
		{
			return m_Queue.get();
		}

		[[nodiscard]] MTL::SharedEvent*
		GetSharedEvent() const noexcept
		{
			return m_Event.get();
		}

		// Drains the waits recorded since the last Open onto `cmdBuffer`. Called by
		// CommandList::Open before it records anything.
		void
		EncodePendingWaits(MTL::CommandBuffer* cmdBuffer) const noexcept;

	private:
		struct PendingWait
		{
			NS::SharedPtr<MTL::SharedEvent> event;
			uint64_t                        value = 0;
		};

		NS::SharedPtr<MTL::CommandQueue> m_Queue;
		NS::SharedPtr<MTL::SharedEvent>  m_Event;
		uint64_t                         m_NextFenceValue = 1;

		// Mutable because two of the three InsertWait* overloads are const on the RHI -- they do not
		// mutate the queue on D3D12, where the wait goes straight to the queue object.
		mutable std::vector<PendingWait> m_PendingWaits;
	};
}
