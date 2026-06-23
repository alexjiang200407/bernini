#pragma once
#include <core/containers/multi_slot_handle.h>

namespace core
{
	template <typename T>
	concept MultiSlotElementConcept =
		std::is_default_constructible_v<T> &&
		(std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>);

	template <MultiSlotElementConcept T>
	class multi_slot_vector
	{
	public:
		multi_slot_vector() noexcept = default;
		explicit multi_slot_vector(uint32_t slotCount) { reset(slotCount); }

		void
		reset(uint32_t slotCount)
		{
			m_Data.clear();
			m_Meta.clear();
			m_FreeSegments.clear();
			m_MaxSlots = slotCount;

			if (slotCount == 0)
				return;

			m_Data.resize(slotCount);
			m_Meta.resize(slotCount);

			// Initially, the entire vector is one big free segment
			m_FreeSegments.push_back({ 0, slotCount });
		}

		[[nodiscard]] void*
		data() const noexcept
		{
			return const_cast<T*>(m_Data.data());
		}

		[[nodiscard]] multi_slot_handle
		allocate_slots(uint32_t count)
		{
			assert(count > 0 && "Cannot allocate 0 slots");

			uint32_t targetIndex = multi_slot_handle::invalid_index;

			for (auto it = m_FreeSegments.begin(); it != m_FreeSegments.end(); ++it)
			{
				if (it->count >= count)
				{
					targetIndex = it->offset;

					if (it->count == count)
					{
						m_FreeSegments.erase(it);
					}
					else
					{
						it->offset += count;
						it->count -= count;
					}
					break;
				}
			}

			if (targetIndex == multi_slot_handle::invalid_index)
			{
				uint32_t currentSize = static_cast<uint32_t>(m_Data.size());
				if (m_MaxSlots != 0 && currentSize + count <= m_MaxSlots)
				{
					throw std::runtime_error(
						"Allocation exceeds multi_slot_vector capacity limits");
				}

				targetIndex = currentSize;
				m_Data.resize(currentSize + count);
				m_Meta.resize(currentSize + count);
			}

			uint32_t generation = m_Meta[targetIndex].generation;

			m_Meta[targetIndex].is_allocated_root = true;
			m_Meta[targetIndex].allocated_count   = count;

			for (uint32_t i = 0; i < count; ++i)
			{
				m_Meta[targetIndex + i].is_active = true;
				m_Data[targetIndex + i]           = T();
			}

			return { targetIndex, count, generation };
		}

		void
		erase(multi_slot_handle handle)
		{
			uint32_t index = handle.index;
			assert(index < m_Meta.size() && "Index out of bounds");
			assert(
				m_Meta[index].is_allocated_root &&
				"Can only erase an allocation using its original starting handle index!");
			assert(
				m_Meta[index].generation == handle.generation &&
				"Attempting to erase using an expired stale handle");

			uint32_t count = m_Meta[index].allocated_count;

			m_Meta[index].is_allocated_root = false;
			m_Meta[index].allocated_count   = 0;

			for (uint32_t i = 0; i < count; ++i)
			{
				uint32_t subIdx          = index + i;
				m_Meta[subIdx].is_active = false;
				m_Meta[subIdx].generation++;

				if constexpr (!std::is_trivially_destructible_v<T>)
				{
					m_Data[subIdx].~T();
				}
				m_Data[subIdx] = T();
			}

			FreeSegment freedSeg{ index, count };

			auto it = std::lower_bound(
				m_FreeSegments.begin(),
				m_FreeSegments.end(),
				freedSeg,
				[](const FreeSegment& lhs, const FreeSegment& rhs) {
					return lhs.offset < rhs.offset;
				});

			auto insertedIt = m_FreeSegments.insert(it, freedSeg);

			auto nextIt = std::next(insertedIt);
			if (nextIt != m_FreeSegments.end() &&
			    (insertedIt->offset + insertedIt->count == nextIt->offset))
			{
				insertedIt->count += nextIt->count;
				m_FreeSegments.erase(nextIt);
			}

			if (insertedIt != m_FreeSegments.begin())
			{
				auto prevIt = std::prev(insertedIt);
				if (prevIt->offset + prevIt->count == insertedIt->offset)
				{
					prevIt->count += insertedIt->count;
					m_FreeSegments.erase(insertedIt);
				}
			}
		}

		[[nodiscard]] T&
		operator[](uint32_t physicalIndex)
		{
			assert(physicalIndex < m_Data.size());
			assert(m_Meta[physicalIndex].is_active);
			return m_Data[physicalIndex];
		}

		[[nodiscard]] const T&
		operator[](uint32_t physicalIndex) const
		{
			assert(physicalIndex < m_Data.size());
			assert(m_Meta[physicalIndex].is_active);
			return m_Data[physicalIndex];
		}

		[[nodiscard]] bool
		valid(uint32_t physicalIndex) const
		{
			if (physicalIndex >= m_Meta.size())
				return false;
			return m_Meta[physicalIndex].is_active;
		}

		[[nodiscard]] size_t
		size() const noexcept
		{
			return m_Data.size();
		}

	private:
		struct slot_meta
		{
			uint32_t generation        = 0;
			uint32_t allocated_count   = 0;
			bool     is_allocated_root = false;
			bool     is_active         = false;
		};

		struct FreeSegment
		{
			uint32_t offset = 0;
			uint32_t count  = 0;
		};

		std::vector<T>           m_Data;
		std::vector<slot_meta>   m_Meta;
		std::vector<FreeSegment> m_FreeSegments;
		uint32_t                 m_MaxSlots = 0;
	};
}
