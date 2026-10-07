#pragma once
#include "native_device_vulkan.h"
#include "volk_vulkan.h"
#include <atomic>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/types/QueueType.h>
#include <core/ref/RefCounter.h>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace bgpu
{
	class ICommandList;

	/**
	 * A queue taken from the context's, with a timeline semaphore for its fence: a fence value is the
	 * semaphore's value, as on D3D12, so every wait -- the CPU's, another queue's, a deferred free's
	 * gate -- is on a number.
	 *
	 * Vulkan waits only inside a submission, so a GPU wait (InsertWait*) is held until the next
	 * ExecuteCommandList or Flush submits it.
	 *
	 * A list is submitted behind the initial transitions of every texture its manager made since the
	 * last submission (ResourceManager::TakeInitialLayouts), recorded into a command buffer of the
	 * queue's own, since the list's allocator may be recording another list by then.
	 */
	class CommandQueue final : public core::RefCounter<ICommandQueue>
	{
	public:
		CommandQueue(GpuContextRef context, QueueType type);
		~CommandQueue() noexcept override;

		CommandQueue(const CommandQueue&) = delete;
		CommandQueue(CommandQueue&&)      = delete;
		CommandQueue&
		operator=(const CommandQueue&) = delete;
		CommandQueue&
		operator=(CommandQueue&&) = delete;

		uint64_t
		ExecuteCommandList(ICommandList* commandList) noexcept override;

		[[nodiscard]] bool
		IsFenceComplete(uint64_t fenceValue) noexcept override;

		[[nodiscard]] uint64_t
		PollCurrentFenceValue() noexcept override;

		[[nodiscard]] uint64_t
		GetLastCompletedFence() const noexcept override
		{
			return m_LastCompletedFenceValue.load(std::memory_order_relaxed);
		}

		[[nodiscard]] uint64_t
		GetNextFenceValue() const noexcept override
		{
			return m_NextFenceValue.load(std::memory_order_relaxed);
		}

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

		[[nodiscard]] double
		GetTimestampFrequency() const noexcept override;

		[[nodiscard]] QueueType
		GetType() const noexcept
		{
			return m_Type;
		}

		[[nodiscard]] uint32_t
		GetFamily() const noexcept
		{
			return m_Queue.family;
		}

		[[nodiscard]] VkSemaphore
		GetVkSemaphore() const noexcept
		{
			return m_Fence;
		}

	private:
		struct PendingWait
		{
			VkSemaphore semaphore = VK_NULL_HANDLE;
			uint64_t    value     = 0;
		};

		// A command buffer of the queue's own, reused once the submission that ran it completes.
		struct Prologue
		{
			VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
			uint64_t        fenceValue    = 0;
		};

		void
		AddWait(VkSemaphore semaphore, uint64_t value) const noexcept;

		/**
		 * A command buffer holding `barriers`, ready to submit at the next fence value.
		 *
		 * @pre m_FenceMutex is held.
		 */
		[[nodiscard]] VkCommandBuffer
		RecordPrologue(std::span<const VkImageMemoryBarrier2> barriers) noexcept;

		/**
		 * Submits `commandBuffers`, in order, behind every held wait, signalling the next fence
		 * value, which it returns.
		 *
		 * @pre m_FenceMutex is held.
		 */
		uint64_t
		SubmitLocked(std::span<const VkCommandBuffer> commandBuffers) noexcept;

		// Declared first, destroyed last: the queue and the semaphore are its device's.
		GpuContextRef m_Context;
		VkDevice      m_Device = VK_NULL_HANDLE;
		VulkanQueue   m_Queue;
		QueueType     m_Type;
		VkSemaphore   m_Fence = VK_NULL_HANDLE;

		// Atomic because the resource manager's sweep reads any registered queue's counters from
		// whichever owner's thread runs it; writes stay under m_FenceMutex.
		std::atomic<uint64_t> m_NextFenceValue          = 1;
		std::atomic<uint64_t> m_LastCompletedFenceValue = 0;

		// Serializes submissions and the held waits. InsertWait* is const on the interface.
		mutable std::mutex               m_FenceMutex;
		mutable std::vector<PendingWait> m_PendingWaits;

		// Under m_FenceMutex.
		VkCommandPool         m_ProloguePool = VK_NULL_HANDLE;
		std::vector<Prologue> m_Prologues;
	};
}
