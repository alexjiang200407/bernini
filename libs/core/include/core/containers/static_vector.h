#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
namespace core
{
	template <typename T, uint32_t MaxElements>
	struct static_vector : private std::array<T, MaxElements>
	{
		typedef std::array<T, MaxElements> base;
		enum
		{
			max_elements = MaxElements
		};

		using typename base::const_iterator;
		using typename base::const_pointer;
		using typename base::const_reference;
		using typename base::difference_type;
		using typename base::iterator;
		using typename base::pointer;
		using typename base::reference;
		using typename base::size_type;
		using typename base::value_type;

		static_vector() : base(), m_CurrentSize(0) {}

		static_vector(size_t size) : base(), m_CurrentSize(size) { assert(size <= max_elements); }

		static_vector(std::initializer_list<T> il) : m_CurrentSize(0)
		{
			for (auto i : il) push_back(i);
		}

		using base::at;

		reference
		operator[](size_type pos)
		{
			assert(pos < m_CurrentSize);
			return base::operator[](pos);
		}

		const_reference
		operator[](size_type pos) const
		{
			assert(pos < m_CurrentSize);
			return base::operator[](pos);
		}

		using base::front;

		reference
		back() noexcept
		{
			auto tmp = end();
			--tmp;
			return *tmp;
		}
		const_reference
		back() const noexcept
		{
			auto tmp = cend();
			--tmp;
			return *tmp;
		}

		using base::begin;
		using base::cbegin;
		using base::data;

		iterator
		end() noexcept
		{
			return iterator(begin()) + static_cast<difference_type>(m_CurrentSize);
		}
		const_iterator
		end() const noexcept
		{
			return cend();
		}
		const_iterator
		cend() const noexcept
		{
			return const_iterator(cbegin()) + static_cast<difference_type>(m_CurrentSize);
		}

		bool
		empty() const noexcept
		{
			return m_CurrentSize == 0;
		}
		size_t
		size() const noexcept
		{
			return m_CurrentSize;
		}
		constexpr size_t
		max_size() const noexcept
		{
			return max_elements;
		}

		void
		fill(const T& value) noexcept
		{
			base::fill(value);
			m_CurrentSize = max_elements;
		}

		void
		swap(static_vector& other) noexcept
		{
			base::swap(*this);
			std::swap(m_CurrentSize, other.m_CurrentSize);
		}

		void
		push_back(const T& value) noexcept
		{
			assert(m_CurrentSize < max_elements);
			*(data() + m_CurrentSize) = value;
			m_CurrentSize++;
		}

		void
		push_back(T&& value) noexcept
		{
			assert(m_CurrentSize < max_elements);
			*(data() + m_CurrentSize) = std::move(value);
			m_CurrentSize++;
		}

		void
		pop_back() noexcept
		{
			assert(m_CurrentSize > 0);
			m_CurrentSize--;
		}

		void
		resize(size_type newSize) noexcept
		{
			assert(newSize <= max_elements);

			if (m_CurrentSize > newSize)
			{
				for (size_type i = newSize; i < m_CurrentSize; i++) *(data() + i) = T{};
			}
			else
			{
				for (size_type i = m_CurrentSize; i < newSize; i++) *(data() + i) = T{};
			}

			m_CurrentSize = newSize;
		}

		reference
		emplace_back() noexcept
		{
			assert(m_CurrentSize < max_elements);
			++m_CurrentSize;
			back() = T{};
			return back();
		}

	private:
		size_type m_CurrentSize = 0;
	};
}
