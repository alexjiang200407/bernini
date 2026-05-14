#pragma once
#include "cmd/CommandQueue.h"
#include "types/QueueType.h"

namespace bgl
{
	class CommandList;

	class CommandQueueImpl
	{
	public:
		CommandQueueImpl(QueueType type, ID3D12Device* device);
		~CommandQueueImpl() noexcept;

		uint64_t
		ExecuteCommandList(const CommandList& commandList);

		bool
		IsFenceComplete(uint64_t fenceValue);

		uint64_t
		PollCurrentFenceValue();

		uint64_t
		GetLastCompletedFence() const
		{
			return m_LastCompletedFenceValue;
		}

		uint64_t
		GetNextFenceValue() const
		{
			return m_NextFenceValue;
		}

		void
		InsertWait(uint64_t fenceValue);

		void
		InsertWaitForQueueFence(CommandQueue cq, uint64_t fenceValue);

		void
		InsertWaitForQueue(CommandQueue otherQueue);

		ID3D12Fence*
		GetD3D12Fence() const
		{
			return m_Fence.Get();
		}

		ID3D12CommandQueue*
		GetD3D12CommandQueue() const
		{
			return m_CommandQueue.Get();
		}

		void
		WaitForFenceCPUBlocking(uint64_t fenceValue);

	private:
		QueueType                       m_Type;
		wrl::ComPtr<ID3D12CommandQueue> m_CommandQueue;

		std::mutex m_FenceMutex;
		std::mutex m_EventMutex;

		wrl::ComPtr<ID3D12Fence> m_Fence;
		uint64_t                 m_NextFenceValue          = 1;
		uint64_t                 m_LastCompletedFenceValue = 0;
		HANDLE                   m_FenceEvent              = nullptr;

		friend class DeviceImpl;
		friend class CommandQueue;
	};
}
