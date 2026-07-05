#pragma once
#include <core/type_traits.h>

namespace core
{
	template <core::type_traits::default_constructible T>
	class fixed_buffer
	{
	public:
		fixed_buffer() noexcept = default;

		explicit fixed_buffer(size_t size) :
			m_Data(size ? std::make_unique_for_overwrite<T[]>(size) : nullptr), m_Size(size)
		{}

		fixed_buffer(fixed_buffer&&) noexcept = default;

		fixed_buffer&
		operator=(fixed_buffer&&) noexcept = default;

		fixed_buffer(const fixed_buffer&) = delete;

		fixed_buffer&
		operator=(const fixed_buffer&) = delete;

		[[nodiscard]] T*
		data() noexcept
		{
			return m_Data.get();
		}

		[[nodiscard]] const T*
		data() const noexcept
		{
			return m_Data.get();
		}

		[[nodiscard]] size_t
		size() const noexcept
		{
			return m_Size;
		}

		[[nodiscard]] bool
		empty() const noexcept
		{
			return m_Size == 0;
		}

		[[nodiscard]] T&
		operator[](size_t index) noexcept
		{
			return m_Data[index];
		}

		[[nodiscard]] const T&
		operator[](size_t index) const noexcept
		{
			return m_Data[index];
		}

		[[nodiscard]] T*
		begin() noexcept
		{
			return m_Data.get();
		}

		[[nodiscard]] T*
		end() noexcept
		{
			return m_Data.get() + m_Size;
		}

		[[nodiscard]] const T*
		begin() const noexcept
		{
			return m_Data.get();
		}

		[[nodiscard]] const T*
		end() const noexcept
		{
			return m_Data.get() + m_Size;
		}

	private:
		std::unique_ptr<T[]> m_Data;
		size_t               m_Size = 0;
	};
}
