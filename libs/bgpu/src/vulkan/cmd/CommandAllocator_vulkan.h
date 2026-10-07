#pragma once
#include "volk_vulkan.h"
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <core/ref/RefCounter.h>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace bgpu
{
	/**
	 * What D3D12's command allocator is: the memory a list records into. A command pool per queue
	 * family, since a list learns its family only from the queue it is opened on, and the descriptor
	 * pools a list's per-dispatch constant-buffer sets come from. ResetAllocator recycles all of it,
	 * under D3D12's precondition that the work recorded from it has completed.
	 *
	 * Single-thread affine, as the list recording into it is.
	 */
	class CommandAllocator final : public core::RefCounter<ICommandAllocator>
	{
	public:
		explicit CommandAllocator(GpuContextRef context);
		~CommandAllocator() noexcept override;

		CommandAllocator(const CommandAllocator&) = delete;
		CommandAllocator(CommandAllocator&&)      = delete;
		CommandAllocator&
		operator=(const CommandAllocator&) = delete;
		CommandAllocator&
		operator=(CommandAllocator&&) = delete;

		void
		ResetAllocator() noexcept override;

		/** A primary command buffer of `family`'s pool, not handed out since the last reset. */
		[[nodiscard]] VkCommandBuffer
		TakeCommandBuffer(uint32_t family) noexcept;

		/** A descriptor set of `layout`, valid until the next reset. */
		[[nodiscard]] VkDescriptorSet
		AllocateSet(VkDescriptorSetLayout layout) noexcept;

	private:
		struct FamilyPool
		{
			VkCommandPool                pool = VK_NULL_HANDLE;
			std::vector<VkCommandBuffer> buffers;
			size_t                       taken = 0;
		};

		void
		AddDescriptorPool() noexcept;

		// Declared first, destroyed last: every pool below is its device's.
		GpuContextRef                            m_Context;
		VkDevice                                 m_Device = VK_NULL_HANDLE;
		std::unordered_map<uint32_t, FamilyPool> m_CommandPools;
		std::vector<VkDescriptorPool>            m_DescriptorPools;
		size_t                                   m_CurrentDescriptorPool = 0;
	};
}
