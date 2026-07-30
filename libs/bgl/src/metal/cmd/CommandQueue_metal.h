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

	private:
		NS::SharedPtr<MTL::CommandQueue> m_Queue;
		NS::SharedPtr<MTL::SharedEvent>  m_Event;
		uint64_t                         m_NextFenceValue = 1;
	};
}
