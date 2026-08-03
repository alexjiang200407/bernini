#pragma once

namespace core
{
	template <class E, class U = std::underlying_type_t<E>>
	class enum_set
	{
	public:
		using enum_type       = E;
		using underlying_type = U;

		static_assert(std::is_enum_v<E>, "enum_set<E, ...> must be an enum");
		static_assert(std::is_integral_v<U>, "enum_set<..., U> must be an integral");

		constexpr enum_set() noexcept                = default;
		constexpr enum_set(const enum_set&) noexcept = default;
		constexpr enum_set(enum_set&&) noexcept      = default;
		explicit constexpr enum_set(U value) noexcept : m_Impl(value) {}

		template <class U2>
		constexpr enum_set(enum_set<E, U2> rhs) noexcept : m_Impl(static_cast<U>(rhs.Get()))
		{}

		template <class... Args>
		constexpr enum_set(Args... values) noexcept
			requires(std::same_as<Args, E> && ...)
			: m_Impl((static_cast<U>(values) | ...))
		{}

		~enum_set() noexcept = default;

		constexpr enum_set&
		operator=(const enum_set&) noexcept = default;
		constexpr enum_set&
		operator=(enum_set&&) noexcept = default;

		template <class U2>
		constexpr enum_set&
		operator=(enum_set<E, U2> rhs) noexcept
		{
			m_Impl = static_cast<U>(rhs.Get());
			return *this;
		}

		constexpr enum_set&
		operator=(E value) noexcept
		{
			m_Impl = static_cast<U>(value);
			return *this;
		}

		constexpr enum_set&
		operator&=(const enum_set& rhs) noexcept
		{
			m_Impl &= rhs.m_Impl;
			return *this;
		}

		constexpr enum_set
		operator&(const enum_set& rhs) const noexcept
		{
			return enum_set{ m_Impl & rhs.m_Impl };
		}

		[[nodiscard]]
		constexpr bool
		operator==(E e) const noexcept
		{
			return m_Impl == static_cast<U>(e);
		}

		[[nodiscard]]
		constexpr bool
		operator==(const enum_set& rhs) const noexcept
		{
			return m_Impl == rhs.m_Impl;
		}

		constexpr enum_set&
		operator|=(const enum_set& rhs) noexcept
		{
			m_Impl |= rhs.m_Impl;
			return *this;
		}

		constexpr enum_set
		operator|(const enum_set& rhs) const noexcept
		{
			return enum_set{ m_Impl | rhs.m_Impl };
		}

		constexpr enum_set
		operator^(const enum_set& rhs) const noexcept
		{
			return enum_set{ m_Impl ^ rhs.m_Impl };
		}

		constexpr enum_set&
		operator^=(const enum_set& rhs) noexcept
		{
			m_Impl ^= rhs.m_Impl;
			return *this;
		}

		constexpr enum_set
		operator~() const noexcept
		{
			return enum_set{ ~m_Impl };
		}

	public:
		[[nodiscard]] explicit constexpr
		operator bool() const noexcept
		{
			return m_Impl != static_cast<U>(0);
		}

		[[nodiscard]] constexpr E
		operator*() const noexcept
		{
			return get();
		}

		[[nodiscard]] constexpr bool
		empty() const noexcept
		{
			return m_Impl == static_cast<U>(0);
		}

		[[nodiscard]] constexpr E
		get() const noexcept
		{
			return static_cast<E>(m_Impl);
		}

		[[nodiscard]] constexpr U
		underlying() const noexcept
		{
			return m_Impl;
		}

	public:
		template <class... Args>
		constexpr enum_set&
		set(Args... args) noexcept
			requires(std::same_as<Args, E> && ...)
		{
			m_Impl |= (static_cast<U>(args) | ...);
			return *this;
		}

		template <class... Args>
		constexpr enum_set&
		set(bool enable, Args... args) noexcept
			requires(std::same_as<Args, E> && ...)
		{
			if (enable)
				m_Impl |= (static_cast<U>(args) | ...);
			else
				m_Impl &= ~(static_cast<U>(args) | ...);

			return *this;
		}

		template <class... Args>
		constexpr enum_set&
		reset(Args... args) noexcept
			requires(std::same_as<Args, E> && ...)
		{
			m_Impl &= ~(static_cast<U>(args) | ...);
			return *this;
		}

		constexpr enum_set&
		reset() noexcept
		{
			m_Impl = 0;
			return *this;
		}

		template <class... Args>
		[[nodiscard]] constexpr bool
		any(Args... args) const noexcept
			requires(std::same_as<Args, E> && ...)
		{
			return (m_Impl & (static_cast<U>(args) | ...)) != static_cast<U>(0);
		}

		template <class... Args>
		[[nodiscard]] constexpr bool
		all(Args... args) const noexcept
			requires(std::same_as<Args, E> && ...)
		{
			return (m_Impl & (static_cast<U>(args) | ...)) == (static_cast<U>(args) | ...);
		}

		[[nodiscard]] constexpr bool
		all(const enum_set<E, U>& rhs) const noexcept
		{
			return (m_Impl & rhs.m_Impl) == rhs.m_Impl;
		}

	private:
		U m_Impl{ 0 };
	};

	template <class... Args>
	enum_set(Args...) -> enum_set<
		std::common_type_t<Args...>,
		std::underlying_type_t<std::common_type_t<Args...>>>;
}
