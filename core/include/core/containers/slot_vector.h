#pragma once
#include <core/containers/slot_handle.h>

namespace core
{
	template <typename T>
	concept SlotElementConcept = std::is_default_constructible_v<T> &&
	                             (std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>);

	template <SlotElementConcept T>
	class slot_vector
	{
	public:
		explicit slot_vector(uint32_t slotCount = 0) { reset(slotCount); }

		void
		reset(uint32_t slotCount = 0)
		{
			m_Data.clear();
			m_Meta.clear();
			m_FreeIndices.clear();
			m_MaxSlots = slotCount;

			if (slotCount == 0)
				return;

			m_Data.resize(slotCount);
			m_Meta.resize(slotCount);
			m_FreeIndices.reserve(slotCount);

			for (uint32_t i = 0; i < slotCount; ++i)
			{
				m_FreeIndices.push_back(slotCount - 1 - i);
			}
		}

		[[nodiscard]] void*
		data() const noexcept
		{
			return const_cast<T*>(m_Data.data());
		}

		[[nodiscard]] slot_handle
		allocate_slot()
		{
			return allocate_and_emplace();
		}

		template <typename... Args>
		[[nodiscard]] slot_handle
		allocate_and_emplace(Args&&... args)
		{
			uint32_t index      = slot_handle::invalid_index;
			uint32_t generation = 0;

			if (!m_FreeIndices.empty())
			{
				index = m_FreeIndices.back();
				m_FreeIndices.pop_back();

				generation                 = m_Meta[index].generation;
				m_Meta[index].is_allocated = true;

				m_Data[index] = T(std::forward<Args>(args)...);

				return { index, generation };
			}

			if (m_MaxSlots != 0)
			{
				assert(m_Data.size() < m_MaxSlots);
			}

			index = static_cast<uint32_t>(m_Data.size());

			m_Data.emplace_back(std::forward<Args>(args)...);

			slot_meta meta;
			meta.generation   = 0;
			meta.is_allocated = true;
			m_Meta.push_back(meta);

			return { index, generation };
		}

		void
		release_slot(uint32_t index)
		{
			assert(index < m_Data.size());
			assert(m_Meta[index].is_allocated);

			if constexpr (!std::is_trivially_destructible_v<T>)
			{
				m_Data[index].~T();
			}
			m_Data[index] = T();

			m_Meta[index].is_allocated = false;
			m_Meta[index].generation++;
			m_FreeIndices.push_back(index);
		}

		[[nodiscard]] bool
		valid(uint32_t index, uint32_t generation) const
		{
			if (index >= m_Data.size())
				return false;

			return m_Meta[index].generation == generation && m_Meta[index].is_allocated;
		}

		[[nodiscard]] T&
		operator[](uint32_t index)
		{
			assert(index < m_Data.size());
			assert(m_Meta[index].is_allocated);
			return m_Data[index];
		}

		[[nodiscard]] const T&
		operator[](uint32_t index) const
		{
			assert(index < m_Data.size());
			assert(m_Meta[index].is_allocated);
			return m_Data[index];
		}

		[[nodiscard]] uint32_t
		generation(uint32_t index) const
		{
			assert(index < m_Meta.size());
			return m_Meta[index].generation;
		}

		[[nodiscard]] size_t
		size() const noexcept
		{
			return m_Data.size();
		}

		void
		clear() noexcept
		{
			m_Data.clear();
			m_Meta.clear();
			m_FreeIndices.clear();
			m_MaxSlots = 0;
		}

	private:
		struct slot_meta
		{
			uint32_t generation   = 0;
			bool     is_allocated = false;
		};

		std::vector<T>         m_Data;
		std::vector<slot_meta> m_Meta;
		std::vector<uint32_t>  m_FreeIndices;
		uint32_t               m_MaxSlots = 0;
	};
}
