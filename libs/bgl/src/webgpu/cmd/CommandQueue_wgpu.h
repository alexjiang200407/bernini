#pragma once

#include "cmd/CommandQueue.h"

namespace bgl
{
	/**
	 * Emulates the RHI's fence timeline on WebGPU, which has no fence object.
	 *
	 * Each submission takes the next value off a monotonic counter and registers a
	 * work-done callback that publishes it; the callbacks land in submission order, so the
	 * highest published value is the completed one. Polling is not free-running -- WebGPU
	 * only runs callbacks when the instance is pumped, which is why PollCurrentFenceValue
	 * processes events rather than just reading the counter.
	 *
	 * WebGPU has one queue per device and orders submissions on it, so the cross-queue waits
	 * are no-ops here rather than unimplemented: there is no second timeline to wait on.
	 */
	class CommandQueue final : public core::RefCounter<ICommandQueue>
	{
	public:
		CommandQueue(WGPUInstance instance, WGPUQueue queue) noexcept;

		~CommandQueue() noexcept override;

		uint64_t
		ExecuteCommandList(ICommandList* commandList) noexcept override;

		[[nodiscard]] bool
		IsFenceComplete(uint64_t fenceValue) noexcept override;

		[[nodiscard]] uint64_t
		PollCurrentFenceValue() noexcept override;

		[[nodiscard]] uint64_t
		GetLastCompletedFence() const noexcept override;

		[[nodiscard]] uint64_t
		GetNextFenceValue() const noexcept override;

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

		[[nodiscard]] WGPUQueue
		GetHandle() const noexcept
		{
			return m_Queue;
		}

	private:
		struct Submission
		{
			uint64_t   value;
			WGPUFuture future;
		};

		struct Completion
		{
			CommandQueue* queue;
			uint64_t      value;
		};

		// Publishes `value` as completed, keeping the counter monotonic.
		void
		Publish(uint64_t value) noexcept;

		[[nodiscard]] std::vector<WGPUFutureWaitInfo>
		PendingUpTo(uint64_t fenceValue) const noexcept;

		WGPUInstance m_Instance = nullptr;
		WGPUQueue    m_Queue    = nullptr;

		std::atomic<uint64_t> m_NextFenceValue     = 1;
		std::atomic<uint64_t> m_LastCompletedFence = 0;

		mutable std::mutex      m_PendingMutex;
		std::vector<Submission> m_Pending;
	};
}
