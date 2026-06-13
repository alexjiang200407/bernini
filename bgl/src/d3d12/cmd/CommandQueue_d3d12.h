#pragma once
#include "cmd/CommandQueue.h"
#include "types/QueueType.h"

namespace bgl
{
	class CommandQueue : public core::RefCounter<ICommandQueue>
	{
	public:
		CommandQueue(QueueType type, ID3D12Device* device);
		~CommandQueue() noexcept;

		CommandQueue(const CommandQueue&) noexcept = delete;
		CommandQueue(CommandQueue&&) noexcept      = delete;

		CommandQueue&
		operator=(const CommandQueue&) noexcept = delete;

		CommandQueue&
		operator=(CommandQueue&&) noexcept = delete;

		uint64_t
		ExecuteCommandList(ICommandList* commandList) override;

		bool
		IsFenceComplete(uint64_t fenceValue) override;

		uint64_t
		PollCurrentFenceValue() override;

		uint64_t
		GetLastCompletedFence() const override
		{
			return m_LastCompletedFenceValue;
		}

		uint64_t
		GetNextFenceValue() const override
		{
			return m_NextFenceValue;
		}

		void
		InsertWait(uint64_t fenceValue) override;

		void
		InsertWaitForQueueFence(ICommandQueue* cq, uint64_t fenceValue) const override;

		void
		InsertWaitForQueue(ICommandQueue* otherQueue) const override;

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

		HANDLE
		GetD3D12FenceEvent() const { return m_FenceEvent; }

		void
		WaitForFenceCPUBlocking(uint64_t fenceValue) override;

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
