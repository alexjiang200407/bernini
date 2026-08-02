#pragma once

namespace bgl
{
	/**
	 * Owns one descriptor heap and hands out indices into it, so which descriptor a resource
	 * occupies is decided here rather than by its slot in a resource pool.
	 *
	 * Not thread-safe: callers serialize access.
	 */
	class DescriptorAllocator final
	{
	public:
		DescriptorAllocator(
			ID3D12Device*               device,
			D3D12_DESCRIPTOR_HEAP_TYPE  type,
			uint32_t                    capacity,
			D3D12_DESCRIPTOR_HEAP_FLAGS flags);

		DescriptorAllocator(const DescriptorAllocator&)     = delete;
		DescriptorAllocator(DescriptorAllocator&&) noexcept = default;

		DescriptorAllocator&
		operator=(const DescriptorAllocator&) = delete;

		DescriptorAllocator&
		operator=(DescriptorAllocator&&) noexcept = default;

		/**
		 * @return An index no other live allocation holds.
		 * @throws std::runtime_error when the heap is full.
		 */
		[[nodiscard]]
		uint32_t
		Allocate();

		/** Returns the index to the free list. Asserts if it is not currently allocated. */
		void
		Free(uint32_t index) noexcept;

		[[nodiscard]]
		D3D12_CPU_DESCRIPTOR_HANDLE
		GetCpuHandle(uint32_t index) const noexcept;

		[[nodiscard]]
		ID3D12DescriptorHeap*
		GetHeap() const noexcept
		{
			return m_Heap.Get();
		}

		[[nodiscard]]
		uint32_t
		GetCapacity() const noexcept
		{
			return m_Capacity;
		}

	private:
		wrl::ComPtr<ID3D12DescriptorHeap> m_Heap;
		D3D12_CPU_DESCRIPTOR_HANDLE       m_HeapStart     = {};
		uint32_t                          m_IncrementSize = 0;
		uint32_t                          m_Capacity      = 0;
		uint32_t                          m_NextUntouched = 0;
		std::vector<uint32_t>             m_FreeIndices;
		std::vector<bool>                 m_Allocated;
	};
}
