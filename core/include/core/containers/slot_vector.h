#pragma once

namespace core
{
	template <typename T>
	class slot_vector
	{
	public:
		struct slot_handle
		{
			uint32_t index      = invalid_index;
			uint32_t generation = 0;

			[[nodiscard]] bool
			is_null() const
			{
				return index == invalid_index;
			}
		};

		explicit slot_vector(uint32_t slotCount = 0)
		{
			reset(slotCount);
		}

		void
		reset(uint32_t slotCount = 0)
		{
			m_Slots.clear();
			m_FreeIndices.clear();
			m_MaxSlots = slotCount;

			if (slotCount == 0)
				return;

			m_Slots.resize(slotCount);
			m_FreeIndices.reserve(slotCount);

			for (uint32_t i = 0; i < slotCount; ++i)
			{
				m_FreeIndices.push_back(slotCount - 1 - i);
			}
		}

		[[nodiscard]] slot_handle
		allocate_slot()
		{
			uint32_t index      = invalid_index;
			uint32_t generation = 0;

			if (!m_FreeIndices.empty())
			{
				index = m_FreeIndices.back();
				m_FreeIndices.pop_back();
				generation = m_Slots[index].generation;
				m_Slots[index].value.emplace();
				return { index, generation };
			}

			if (m_MaxSlots != 0)
			{
				assert(m_Slots.size() < m_MaxSlots);
			}

			index = static_cast<uint32_t>(m_Slots.size());
			m_Slots.emplace_back();
			m_Slots.back().value.emplace();
			return { index, generation };
		}

		void
		release_slot(uint32_t index)
		{
			assert(index < m_Slots.size());
			assert(m_Slots[index].value.has_value());

			m_Slots[index].value.reset();
			m_Slots[index].generation++;
			m_FreeIndices.push_back(index);
		}

		[[nodiscard]] bool
		valid(uint32_t index, uint32_t generation) const
		{
			if (index >= m_Slots.size())
				return false;

			return m_Slots[index].generation == generation && m_Slots[index].value.has_value();
		}

		[[nodiscard]] T&
		operator[](uint32_t index)
		{
			assert(index < m_Slots.size());
			assert(m_Slots[index].value.has_value());
			return *m_Slots[index].value;
		}

		[[nodiscard]] const T&
		operator[](uint32_t index) const
		{
			assert(index < m_Slots.size());
			assert(m_Slots[index].value.has_value());
			return *m_Slots[index].value;
		}

		[[nodiscard]] uint32_t
		generation(uint32_t index) const
		{
			assert(index < m_Slots.size());
			return m_Slots[index].generation;
		}

		[[nodiscard]] size_t
		size() const noexcept
		{
			return m_Slots.size();
		}

	private:
		struct slot_entry
		{
			std::optional<T> value;
			uint32_t         generation = 0;
		};

		static constexpr uint32_t invalid_index = 0xFFFFFFFFu;

		std::vector<slot_entry> m_Slots;
		std::vector<uint32_t>   m_FreeIndices;
		uint32_t                m_MaxSlots = 0;
	};
}
