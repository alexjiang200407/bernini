#include "cmd/CommandQueue_d3d12.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_d3d12.h"
#include "cmd/CommandQueue.h"
#include "convert_d3d12.h"

namespace bgl
{
	CommandQueue::CommandQueue(QueueType type, ID3D12Device* device) : m_Type(type)
	{
		gassert(device != nullptr, "Device cannot be null");

		D3D12_COMMAND_QUEUE_DESC cqDesc = {};
		cqDesc.Type                     = ConvertQueueType(type);
		cqDesc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;
		device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&m_CommandQueue)) >> d3d12ErrChecker;

		device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)) >> d3d12ErrChecker;

		m_Fence->Signal(m_LastCompletedFenceValue) >> d3d12ErrChecker;
		m_FenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

		gassert(m_FenceEvent != nullptr, "Failed to create fence event");
	}

	CommandQueue::~CommandQueue() noexcept
	{
		logger::trace("~CommandQueue");
		if (m_FenceEvent)
			CloseHandle(m_FenceEvent);
	}

	uint64_t
	CommandQueue::ExecuteCommandList(ICommandList* commandList) noexcept
	{
		gassert(commandList != nullptr, "Command list is not initialized.");
		gassert(
			commandList->GetType() == m_Type,
			"Command list type must match command queue type");

		auto cmdList = commandList->As<CommandList>()->GetD3D12CommandList();
		m_CommandQueue->ExecuteCommandLists(
			1,
			reinterpret_cast<ID3D12CommandList* const*>(&cmdList));

		commandList->As<CommandList>()->SubmitChunks(this);

		std::lock_guard<std::mutex> lockGuard(m_FenceMutex);
		m_CommandQueue->Signal(m_Fence.Get(), m_NextFenceValue.load(std::memory_order_relaxed)) >>
			d3d12ErrChecker;

		return m_NextFenceValue.fetch_add(1, std::memory_order_relaxed);
	}

	uint64_t
	CommandQueue::PollCurrentFenceValue() noexcept
	{
		// Max-update: concurrent pollers (the RM sweep runs on any context's thread) must never
		// move the published value backwards.
		const uint64_t completed = m_Fence->GetCompletedValue();
		uint64_t       previous  = m_LastCompletedFenceValue.load(std::memory_order_relaxed);
		while (previous < completed && !m_LastCompletedFenceValue.compare_exchange_weak(
										   previous,
										   completed,
										   std::memory_order_relaxed))
		{}
		return std::max(previous, completed);
	}

	void
	CommandQueue::InsertWait(uint64_t fenceValue) noexcept
	{
		m_CommandQueue->Wait(m_Fence.Get(), fenceValue) >> d3d12ErrChecker;
	}

	void
	CommandQueue::InsertWaitForQueueFence(ICommandQueue* cq, uint64_t fenceValue) const noexcept
	{
		m_CommandQueue->Wait(cq->As<CommandQueue>()->m_Fence.Get(), fenceValue) >> d3d12ErrChecker;
	}

	void
	CommandQueue::InsertWaitForQueue(ICommandQueue* otherQueue) const noexcept
	{
		m_CommandQueue->Wait(
			otherQueue->As<CommandQueue>()->m_Fence.Get(),
			otherQueue->GetNextFenceValue() - 1);
	}

	void
	CommandQueue::WaitForFenceCPUBlocking(uint64_t fenceValue) noexcept
	{
		if (IsFenceComplete(fenceValue))
		{
			return;
		}

		{
			std::lock_guard<std::mutex> lockGuard(m_EventMutex);

			m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent);
			WaitForSingleObjectEx(m_FenceEvent, INFINITE, false);

			uint64_t previous = m_LastCompletedFenceValue.load(std::memory_order_relaxed);
			while (previous < fenceValue && !m_LastCompletedFenceValue.compare_exchange_weak(
												previous,
												fenceValue,
												std::memory_order_relaxed))
			{}
		}
	}

	void
	CommandQueue::Flush() noexcept
	{
		uint64_t fenceValue;
		{
			std::lock_guard<std::mutex> lockGuard(m_FenceMutex);
			fenceValue = m_NextFenceValue++;
			m_CommandQueue->Signal(m_Fence.Get(), fenceValue) >> d3d12ErrChecker;
		}

		WaitForFenceCPUBlocking(fenceValue);
	}

	bool
	CommandQueue::IsFenceComplete(uint64_t fenceValue) noexcept
	{
		if (fenceValue > m_LastCompletedFenceValue.load(std::memory_order_relaxed))
		{
			return fenceValue <= PollCurrentFenceValue();
		}

		return true;
	}

}
