#pragma once

namespace core
{
	template <typename T>
	concept PackedElementConcept = std::is_default_constructible_v<T> &&
	                               (std::is_move_assignable_v<T> || std::is_copy_assignable_v<T>);

	// A dense, contiguous container with swap-and-pop erase. Erasing an element
	// moves the last element into the vacated slot so the live range [0, size())
	// stays packed with no holes, which keeps linear iteration cheap.
	//
	// Because of the swap, element indices are NOT stable across erase(): the
	// element that was last is relocated. Callers that keep external indices must
	// fix them up using the index erase() returns. This is intended for "root"
	// data that nothing else references by index.
	template <PackedElementConcept T>
	class packed_vector
	{
	public:
		static constexpr uint32_t invalid_index = 0xFFFFFFFFu;

		packed_vector() noexcept = default;
		explicit packed_vector(uint32_t capacity) { reset(capacity); }

		void
		reset(uint32_t capacity)
		{
			m_Data.clear();
			m_Count    = 0;
			m_Capacity = capacity;

			// Keep the backing storage sized to capacity so data() spans the whole
			// buffer (mirrors slot_vector / multi_slot_vector).
			m_Data.resize(capacity);
		}

		template <typename... Args>
		uint32_t
		emplace_back(Args&&... args)
		{
			if (m_Capacity != 0)
			{
				assert(m_Count < m_Capacity && "packed_vector capacity exceeded");
			}

			uint32_t index = m_Count;

			if (index < m_Data.size())
			{
				m_Data[index] = T(std::forward<Args>(args)...);
			}
			else
			{
				m_Data.emplace_back(std::forward<Args>(args)...);
			}

			++m_Count;
			return index;
		}

		uint32_t
		push_back(T value)
		{
			return emplace_back(std::move(value));
		}

		// Removes the element at `index` by moving the last live element into its
		// slot. Returns the index that the moved element previously occupied (the
		// old last index), or invalid_index when the erased element was already
		// last (nothing moved).
		uint32_t
		erase(uint32_t index)
		{
			assert(index < m_Count && "erase index out of range");

			const uint32_t last  = m_Count - 1;
			uint32_t       moved = invalid_index;

			if (index != last)
			{
				m_Data[index] = std::move(m_Data[last]);
				moved         = last;
			}

			if constexpr (!std::is_trivially_destructible_v<T>)
			{
				m_Data[last].~T();
			}
			m_Data[last] = T();

			--m_Count;
			return moved;
		}

		[[nodiscard]] T&
		operator[](uint32_t index)
		{
			assert(index < m_Count);
			return m_Data[index];
		}

		[[nodiscard]] const T&
		operator[](uint32_t index) const
		{
			assert(index < m_Count);
			return m_Data[index];
		}

		[[nodiscard]] bool
		valid(uint32_t index) const noexcept
		{
			return index < m_Count;
		}

		[[nodiscard]] void*
		data() const noexcept
		{
			return const_cast<T*>(m_Data.data());
		}

		[[nodiscard]] uint32_t
		size() const noexcept
		{
			return m_Count;
		}

		[[nodiscard]] bool
		empty() const noexcept
		{
			return m_Count == 0;
		}

		[[nodiscard]] uint32_t
		capacity() const noexcept
		{
			return m_Capacity;
		}

		void
		clear() noexcept
		{
			m_Data.clear();
			m_Count    = 0;
			m_Capacity = 0;
		}

	private:
		std::vector<T> m_Data;
		uint32_t       m_Count    = 0;
		uint32_t       m_Capacity = 0;
	};
}
