#pragma once

#include "cmd/CommandQueue.h"

namespace bgl
{
	/**
	 * Emulates the RHI's fence timeline on WebGPU, which has no fence object.
	 *
	 * Each submission takes the next value off a monotonic counter and keeps the future that
	 * `wgpuQueueOnSubmittedWorkDone` returns for it. The fence only advances when the CPU waits:
	 * `wgpuInstanceWaitAny` reports which of those futures have completed, and because the queue
	 * runs submissions in order, the highest completed value is the completed fence. There is no
	 * work-done callback to publish it -- the wait's own result is read instead.
	 *
	 * WebGPU has one queue per device and orders submissions on it, so the cross-queue waits are
	 * no-ops here rather than unimplemented: there is no second timeline to wait on.
	 */
	class CommandQueue final : public core::RefCounter<ICommandQueue>
	{
	public:
		CommandQueue(const wgpu::Instance& instance, const wgpu::Queue& queue) noexcept;

		~CommandQueue() noexcept override;

		CommandQueue(const CommandQueue&) noexcept = delete;
		CommandQueue(CommandQueue&&) noexcept      = delete;

		CommandQueue&
		operator=(const CommandQueue&) noexcept = delete;

		CommandQueue&
		operator=(CommandQueue&&) noexcept = delete;

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

		[[nodiscard]] const wgpu::Queue&
		GetHandle() const noexcept
		{
			return m_Queue;
		}

	private:
		struct Submission
		{
			uint64_t     value;
			wgpu::Future future;
		};

		// Advances the completed fence to `value`, never backwards.
		void
		Publish(uint64_t value) noexcept;

		// Waits (up to `timeoutNs`) on every pending submission at or below `upTo`, publishes the
		// ones that completed, drops them, and returns the completed fence. A zero timeout polls.
		uint64_t
		DrainCompleted(uint64_t upTo, uint64_t timeoutNs) noexcept;

		wgpu::Instance m_Instance;
		wgpu::Queue    m_Queue;

		std::atomic<uint64_t> m_NextFenceValue     = 1;
		std::atomic<uint64_t> m_LastCompletedFence = 0;

		mutable std::mutex      m_PendingMutex;
		std::vector<Submission> m_Pending;
	};
}
