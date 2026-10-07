#pragma once
#include <bgpu/constants/constants.h>
#include <core/err/util.h>
#include <cstdint>
#include <vector>

namespace bgpu
{
	/**
	 * The indices of one shader-visible descriptor table, handed out so that which descriptor a
	 * resource occupies is decided here rather than by its slot in a resource pool. Index
	 * c_UnboundDescriptorIndex is never handed out.
	 *
	 * Not thread-safe: callers serialize access.
	 */
	class DescriptorIndexPool final
	{
	public:
		explicit DescriptorIndexPool(uint32_t capacity) :
			m_Capacity(capacity), m_Allocated(capacity)
		{
			core::ensure(
				capacity > c_UnboundDescriptorIndex,
				"A descriptor table must have room for the sentinel");
			m_FreeIndices.reserve(capacity);

			// Marked allocated so Free asserts rather than releasing it into the free list.
			m_NextUntouched                       = c_UnboundDescriptorIndex + 1;
			m_Allocated[c_UnboundDescriptorIndex] = true;
		}

		/**
		 * @return An index no other live allocation holds.
		 * @throws std::runtime_error when the table is full.
		 */
		[[nodiscard]] uint32_t
		Allocate()
		{
			if (!m_FreeIndices.empty())
			{
				const auto index = m_FreeIndices.back();
				m_FreeIndices.pop_back();
				m_Allocated[index] = true;
				return index;
			}

			if (m_NextUntouched >= m_Capacity)
				core::throw_runtime_error("descriptor table is full ({} descriptors)", m_Capacity);

			const auto index   = m_NextUntouched++;
			m_Allocated[index] = true;
			return index;
		}

		/** Returns the index to the free list. Asserts if it is not currently allocated. */
		void
		Free(uint32_t index) noexcept
		{
			core::ensure(index < m_Capacity, "Descriptor index {} out of range", index);
			core::ensure(
				m_Allocated[index],
				"Freeing descriptor index {}, which is not allocated",
				index);

			m_Allocated[index] = false;
			m_FreeIndices.push_back(index);
		}

		[[nodiscard]] uint32_t
		GetCapacity() const noexcept
		{
			return m_Capacity;
		}

	private:
		uint32_t              m_Capacity      = 0;
		uint32_t              m_NextUntouched = 0;
		std::vector<uint32_t> m_FreeIndices;
		std::vector<bool>     m_Allocated;
	};
}
