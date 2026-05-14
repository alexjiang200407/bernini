#include "cmd/CommandQueue_d3d12.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_d3d12.h"
#include "cmd/CommandQueue.h"
#include "util.h"

namespace bgl
{
	CommandQueueImpl::CommandQueueImpl(QueueType type, ID3D12Device* device) : m_Type(type)
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

	CommandQueueImpl::~CommandQueueImpl() noexcept
	{
		if (m_FenceEvent)
			CloseHandle(m_FenceEvent);
	}

	uint64_t
	CommandQueueImpl::ExecuteCommandList(const CommandList& commandList)
	{
		gassert(commandList.IsInitialized(), "Command list is not initialized.");
		gassert(commandList.GetType() == m_Type, "Command list type must match command queue type");

		auto cmdList = commandList->GetCommandList();
		m_CommandQueue->ExecuteCommandLists(
			1,
			reinterpret_cast<ID3D12CommandList* const*>(&cmdList));

		std::lock_guard<std::mutex> lockGuard(m_FenceMutex);
		m_CommandQueue->Signal(m_Fence.Get(), m_NextFenceValue) >> d3d12ErrChecker;

		return m_NextFenceValue++;
	}

	uint64_t
	CommandQueueImpl::PollCurrentFenceValue()
	{
		m_LastCompletedFenceValue =
			std::max(m_LastCompletedFenceValue, m_Fence->GetCompletedValue());
		return m_LastCompletedFenceValue;
	}

	void
	CommandQueueImpl::InsertWait(uint64_t fenceValue)
	{
		m_CommandQueue->Wait(m_Fence.Get(), fenceValue) >> d3d12ErrChecker;
	}

	void
	CommandQueueImpl::InsertWaitForQueueFence(CommandQueue cq, uint64_t fenceValue)
	{
		m_CommandQueue->Wait(cq->m_Fence.Get(), fenceValue) >> d3d12ErrChecker;
	}

	void
	CommandQueueImpl::InsertWaitForQueue(CommandQueue otherQueue)
	{
		m_CommandQueue->Wait(otherQueue->m_Fence.Get(), otherQueue->GetNextFenceValue() - 1);
	}

	void
	CommandQueueImpl::WaitForFenceCPUBlocking(uint64_t fenceValue)
	{
		if (IsFenceComplete(fenceValue))
		{
			return;
		}

		{
			std::lock_guard<std::mutex> lockGuard(m_EventMutex);

			m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent);
			WaitForSingleObjectEx(m_FenceEvent, INFINITE, false);
			m_LastCompletedFenceValue = fenceValue;
		}
	}

	bool
	CommandQueueImpl::IsFenceComplete(uint64_t fenceValue)
	{
		if (fenceValue > m_LastCompletedFenceValue)
		{
			PollCurrentFenceValue();
		}

		return fenceValue <= m_LastCompletedFenceValue;
	}

	bool
	CommandQueue::IsFenceComplete(uint64_t fenceValue) const
	{
		gassert(IsInitialized(), "Command queue implementation is not initialized.");
		return GetImpl()->IsFenceComplete(fenceValue);
	}

	uint64_t
	CommandQueue::PollCurrentFenceValue() const
	{
		gassert(IsInitialized(), "Command queue is not initialized.");
		return GetImpl()->PollCurrentFenceValue();
	}

	uint64_t
	CommandQueue::GetLastCompletedFence() const
	{
		gassert(IsInitialized(), "Command queue is not initialized.");
		return GetImpl()->GetLastCompletedFence();
	}

	uint64_t
	CommandQueue::GetNextFenceValue() const
	{
		gassert(IsInitialized(), "Command queue is not initialized.");
		return GetImpl()->GetNextFenceValue();
	}

	void
	CommandQueue::InsertWait(uint64_t fenceValue)
	{
		gassert(IsInitialized(), "Command queue is not initialized.");
		GetImpl()->InsertWait(fenceValue);
	}

	void
	CommandQueue::InsertWaitForQueueFence(CommandQueue cq, uint64_t fenceValue)
	{
		gassert(IsInitialized(), "Command queue is not initialized.");
		GetImpl()->InsertWaitForQueueFence(cq, fenceValue);
	}

	void
	CommandQueue::InsertWaitForQueue(CommandQueue otherQueue)
	{
		gassert(IsInitialized(), "Command queue is not initialized.");
		GetImpl()->InsertWaitForQueue(otherQueue);
	}

	void
	CommandQueue::WaitForFenceCPUBlocking(uint64_t fenceValue)
	{
		gassert(IsInitialized(), "Command queue is not initialized.");
		GetImpl()->WaitForFenceCPUBlocking(fenceValue);
	}

	uint64_t
	CommandQueue::ExecuteCommandList(const CommandList& commandList) const
	{
		gassert(IsInitialized(), "Command queue is not initialized.");
		return GetImpl()->ExecuteCommandList(commandList);
	}
}
