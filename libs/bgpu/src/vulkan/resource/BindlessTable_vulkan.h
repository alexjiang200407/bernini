#pragma once
#include "resource/DescriptorIndexPool.h"
#include "volk_vulkan.h"
#include <cstdint>

namespace bgpu
{
	/**
	 * A resource manager's shader-visible descriptors: one descriptor set holding the array of
	 * storage buffers Slang lowers a buffer handle to, and the indices into it.
	 *
	 * Every manager's set has the same layout, sized to c_Capacity whatever the manager asked for, so
	 * a pipeline layout made by any device on the context is compatible with every manager's set.
	 * Not thread-safe: the manager serializes it.
	 */
	class BindlessTable final
	{
	public:
		// Where Slang's SPIR-V reaches the table: the set the sessions pin the bindless space to, and
		// the binding its buffer handles index.
		static constexpr uint32_t c_Set           = 1;
		static constexpr uint32_t c_BufferBinding = 2;

		// Descriptors in every table's layout. A manager hands out at most maxCbvSrvUavs of them.
		static constexpr uint32_t c_Capacity = 1U << 16U;

		/** The set's layout, for a pipeline layout to name. The caller destroys it. */
		[[nodiscard]] static VkDescriptorSetLayout
		CreateSetLayout(VkDevice device) noexcept;

		/** @param indexCount the indices handed out, the sentinel included: at most c_Capacity. */
		BindlessTable(VkDevice device, uint32_t indexCount);
		~BindlessTable() noexcept;

		BindlessTable(const BindlessTable&) = delete;
		BindlessTable(BindlessTable&&)      = delete;
		BindlessTable&
		operator=(const BindlessTable&) = delete;
		BindlessTable&
		operator=(BindlessTable&&) = delete;

		/** @throws std::runtime_error when every index is taken. */
		[[nodiscard]] uint32_t
		Allocate()
		{
			return m_Indices.Allocate();
		}

		void
		Free(uint32_t index) noexcept
		{
			m_Indices.Free(index);
		}

		/** Points descriptor `index` at the whole of `buffer`. */
		void
		WriteBuffer(uint32_t index, VkBuffer buffer) noexcept;

		[[nodiscard]] VkDescriptorSet
		GetSet() const noexcept
		{
			return m_Set;
		}

	private:
		VkDevice              m_Device = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
		VkDescriptorPool      m_Pool   = VK_NULL_HANDLE;
		VkDescriptorSet       m_Set    = VK_NULL_HANDLE;
		DescriptorIndexPool   m_Indices;
	};
}
