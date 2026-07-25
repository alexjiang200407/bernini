#include "cmd/CommandQueue_wgpu.h"

#include "cmd/CommandList_wgpu.h"

namespace bgl
{
	CommandQueue::CommandQueue(WGPUInstance instance, WGPUQueue queue) noexcept :
		m_Instance(instance), m_Queue(queue)
	{
		wgpuInstanceAddRef(m_Instance);
		wgpuQueueAddRef(m_Queue);
	}

	CommandQueue::~CommandQueue() noexcept
	{
		Flush();

		wgpuQueueRelease(m_Queue);
		wgpuInstanceRelease(m_Instance);
	}

	void
	CommandQueue::Publish(uint64_t value) noexcept
	{
		auto seen = m_LastCompletedFence.load(std::memory_order_relaxed);
		while (seen < value &&
		       !m_LastCompletedFence.compare_exchange_weak(seen, value, std::memory_order_relaxed));
	}

	uint64_t
	CommandQueue::ExecuteCommandList(ICommandList* commandList) noexcept
	{
		gassert(commandList != nullptr, "ExecuteCommandList: null command list");

		auto* list = static_cast<CommandList*>(commandList);

		WGPUCommandBuffer buffer = list->TakeCommandBuffer();
		gassert(buffer != nullptr, "ExecuteCommandList: the list was never closed");

		wgpuQueueSubmit(m_Queue, 1, &buffer);
		wgpuCommandBufferRelease(buffer);

		const auto value = m_NextFenceValue.fetch_add(1, std::memory_order_relaxed);

		// A no-op callback: the completion is read from wgpuInstanceWaitAny's result, not from here.
		// WaitAnyOnly is still required so the future is one WaitAny can report on.
		auto info     = WGPUQueueWorkDoneCallbackInfo{};
		info.mode     = WGPUCallbackMode_WaitAnyOnly;
		info.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void*, void*) {};

		const auto future = wgpuQueueOnSubmittedWorkDone(m_Queue, info);

		{
			auto lock = std::scoped_lock(m_PendingMutex);
			m_Pending.push_back({ value, future });
		}

		return value;
	}

	uint64_t
	CommandQueue::DrainCompleted(uint64_t upTo, uint64_t timeoutNs) noexcept
	{
		auto values = std::vector<uint64_t>();
		auto waits  = std::vector<WGPUFutureWaitInfo>();

		{
			auto lock = std::scoped_lock(m_PendingMutex);
			for (const Submission& submission : m_Pending)
			{
				if (submission.value <= upTo)
				{
					values.push_back(submission.value);
					waits.push_back(WGPUFutureWaitInfo{ submission.future, 0 });
				}
			}
		}

		// WaitAny flips `completed` on each future that finished; a zero timeout makes this a poll.
		if (!waits.empty())
		{
			wgpuInstanceWaitAny(m_Instance, waits.size(), waits.data(), timeoutNs);

			for (size_t i = 0; i < waits.size(); ++i)
			{
				if (waits[i].completed)
					Publish(values[i]);
			}
		}

		const auto completed = m_LastCompletedFence.load(std::memory_order_relaxed);

		auto lock = std::scoped_lock(m_PendingMutex);
		std::erase_if(m_Pending, [completed](const Submission& s) { return s.value <= completed; });

		return completed;
	}

	uint64_t
	CommandQueue::PollCurrentFenceValue() noexcept
	{
		return DrainCompleted(UINT64_MAX, 0);
	}

	bool
	CommandQueue::IsFenceComplete(uint64_t fenceValue) noexcept
	{
		if (fenceValue > m_LastCompletedFence.load(std::memory_order_relaxed))
			return fenceValue <= PollCurrentFenceValue();

		return true;
	}

	uint64_t
	CommandQueue::GetLastCompletedFence() const noexcept
	{
		return m_LastCompletedFence.load(std::memory_order_relaxed);
	}

	uint64_t
	CommandQueue::GetNextFenceValue() const noexcept
	{
		return m_NextFenceValue.load(std::memory_order_relaxed);
	}

	void
	CommandQueue::WaitForFenceCPUBlocking(uint64_t fenceValue) noexcept
	{
		// D3D12 blocks on a fence event once; WebGPU has no such primitive, so this loops:
		// wgpuInstanceWaitAny returns as soon as *any* future completes, so it may take several
		// waits to reach the requested value.
		while (fenceValue > m_LastCompletedFence.load(std::memory_order_relaxed))
		{
			bool anyPending;
			{
				auto lock  = std::scoped_lock(m_PendingMutex);
				anyPending = std::ranges::any_of(m_Pending, [fenceValue](const Submission& s) {
					return s.value <= fenceValue;
				});
			}

			// Nothing outstanding at or below the value: it was never submitted, or already drained.
			// Blocking further would never return, so treat the value as reached.
			if (!anyPending)
			{
				Publish(fenceValue);
				return;
			}

			DrainCompleted(fenceValue, UINT64_MAX);
		}
	}

	void
	CommandQueue::Flush() noexcept
	{
		// D3D12 signals a fresh fence past all submitted work and waits for it. WebGPU has no
		// standalone signal -- a future exists only per submission -- so this waits for the last
		// value ExecuteCommandList handed out instead, which is the same "all submitted work done".
		WaitForFenceCPUBlocking(m_NextFenceValue.load(std::memory_order_relaxed) - 1);
	}

	// The cross-queue waits stay no-ops rather than gfatal: WebGPU has a single ordered queue, so a
	// GPU-side wait on this timeline is already satisfied, and there is no other timeline to wait on.
	// A gfatal would turn a benign call from backend-agnostic scheduling code into a crash.

	void
	CommandQueue::InsertWait(uint64_t) noexcept
	{}

	void
	CommandQueue::InsertWaitForQueueFence(ICommandQueue*, uint64_t) const noexcept
	{}

	void
	CommandQueue::InsertWaitForQueue(ICommandQueue*) const noexcept
	{}
}
