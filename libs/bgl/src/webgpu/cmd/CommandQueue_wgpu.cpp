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

		// Outlives this call, and is freed by the callback.
		auto* payload = new Completion{ this, value };

		auto info      = WGPUQueueWorkDoneCallbackInfo{};
		info.mode      = WGPUCallbackMode_WaitAnyOnly;
		info.userdata1 = payload;
		info.callback  = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* userdata, void*) {
			auto* completion = static_cast<Completion*>(userdata);
			completion->queue->Publish(completion->value);
			delete completion;
		};

		const auto future = wgpuQueueOnSubmittedWorkDone(m_Queue, info);

		{
			auto lock = std::scoped_lock(m_PendingMutex);
			m_Pending.push_back({ value, future });
		}

		return value;
	}

	std::vector<WGPUFutureWaitInfo>
	CommandQueue::PendingUpTo(uint64_t fenceValue) const noexcept
	{
		auto waits = std::vector<WGPUFutureWaitInfo>();
		auto lock  = std::scoped_lock(m_PendingMutex);

		for (const auto& submission : m_Pending)
		{
			if (submission.value <= fenceValue)
				waits.push_back(WGPUFutureWaitInfo{ submission.future, 0 });
		}

		return waits;
	}

	uint64_t
	CommandQueue::PollCurrentFenceValue() noexcept
	{
		// A WaitAnyOnly callback only runs inside WaitAny, so a zero timeout is how this
		// timeline is polled: it retires whatever is already done and returns immediately.
		auto waits = PendingUpTo(UINT64_MAX);
		if (!waits.empty())
			wgpuInstanceWaitAny(m_Instance, waits.size(), waits.data(), 0);

		const auto completed = m_LastCompletedFence.load(std::memory_order_relaxed);

		auto lock = std::scoped_lock(m_PendingMutex);
		std::erase_if(m_Pending, [completed](const Submission& s) { return s.value <= completed; });

		return completed;
	}

	bool
	CommandQueue::IsFenceComplete(uint64_t fenceValue) noexcept
	{
		if (m_LastCompletedFence.load(std::memory_order_relaxed) >= fenceValue)
			return true;

		return PollCurrentFenceValue() >= fenceValue;
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
		while (m_LastCompletedFence.load(std::memory_order_relaxed) < fenceValue)
		{
			auto waits = PendingUpTo(fenceValue);
			if (waits.empty())
			{
				// Nothing outstanding at or below the value: it was either never submitted or
				// its callback has already run. Blocking further would never return.
				Publish(fenceValue);
				return;
			}

			// Returns as soon as *one* future completes, so the loop is what drains the rest.
			wgpuInstanceWaitAny(m_Instance, waits.size(), waits.data(), UINT64_MAX);

			(void)PollCurrentFenceValue();
		}
	}

	void
	CommandQueue::Flush() noexcept
	{
		WaitForFenceCPUBlocking(m_NextFenceValue.load(std::memory_order_relaxed) - 1);
	}

	void
	CommandQueue::InsertWait(uint64_t) noexcept
	{
		// One queue, and WebGPU orders submissions on it: there is no timeline to wait on.
	}

	void
	CommandQueue::InsertWaitForQueueFence(ICommandQueue*, uint64_t) const noexcept
	{}

	void
	CommandQueue::InsertWaitForQueue(ICommandQueue*) const noexcept
	{}
}
