#pragma once
#include "cmd/CommandQueue.h"
#include "types/QueueType.h"

namespace bgl
{
	class CommandQueue : public core::RefCounter<ICommandQueue>
	{
	public:
		CommandQueue(QueueType type, ID3D12Device* device);
		~CommandQueue() noexcept override;

		CommandQueue(const CommandQueue&) noexcept = delete;
		CommandQueue(CommandQueue&&) noexcept      = delete;

		CommandQueue&
		operator=(const CommandQueue&) noexcept = delete;

		CommandQueue&
		operator=(CommandQueue&&) noexcept = delete;

		uint64_t
		ExecuteCommandList(ICommandList* commandList) noexcept override;

		bool
		IsFenceComplete(uint64_t fenceValue) noexcept override;

		uint64_t
		PollCurrentFenceValue() noexcept override;

		uint64_t
		GetLastCompletedFence() const noexcept override
		{
			return m_LastCompletedFenceValue;
		}

		uint64_t
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

		ID3D12Fence*
		GetD3D12Fence() const noexcept
		{
			return m_Fence.Get();
		}

		ID3D12CommandQueue*
		GetD3D12CommandQueue() const noexcept
		{
			return m_CommandQueue.Get();
		}

		HANDLE
		GetD3D12FenceEvent() const noexcept { return m_FenceEvent; }

		void
		WaitForFenceCPUBlocking(uint64_t fenceValue) noexcept override;

	private:
		QueueType                       m_Type;
		wrl::ComPtr<ID3D12CommandQueue> m_CommandQueue;

		std::mutex m_FenceMutex;
		std::mutex m_EventMutex;

		wrl::ComPtr<ID3D12Fence> m_Fence;
		uint64_t                 m_NextFenceValue          = 1;
		uint64_t                 m_LastCompletedFenceValue = 0;
		HANDLE                   m_FenceEvent              = nullptr;
	};
}
