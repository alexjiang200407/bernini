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
		explicit constexpr enum_set(U a_value) noexcept : _impl(a_value) {}

		template <class U2>
		constexpr enum_set(enum_set<E, U2> a_rhs) noexcept : _impl(static_cast<U>(a_rhs.Get()))
		{}

		template <class... Args>
		constexpr enum_set(Args... a_values) noexcept
			requires(std::same_as<Args, E> && ...)
			: _impl((static_cast<U>(a_values) | ...))
		{}

		~enum_set() noexcept = default;

		constexpr enum_set&
		operator=(const enum_set&) noexcept = default;
		constexpr enum_set&
		operator=(enum_set&&) noexcept = default;

		template <class U2>
		constexpr enum_set&
		operator=(enum_set<E, U2> a_rhs) noexcept
		{
			_impl = static_cast<U>(a_rhs.Get());
			return *this;
		}

		constexpr enum_set&
		operator=(E a_value) noexcept
		{
			_impl = static_cast<U>(a_value);
			return *this;
		}

		constexpr enum_set&
		operator&=(const enum_set& rhs) noexcept
		{
			_impl &= rhs._impl;
			return *this;
		}

		constexpr enum_set
		operator&(const enum_set& rhs) const noexcept
		{
			return enum_set{ _impl & rhs._impl };
		}

		[[nodiscard]]
		constexpr bool
		operator==(E e) const noexcept
		{
			return _impl == static_cast<U>(e);
		}

		[[nodiscard]]
		constexpr bool
		operator==(const enum_set& rhs) const noexcept
		{
			return _impl == rhs._impl;
		}

		constexpr enum_set&
		operator|=(const enum_set& rhs) noexcept
		{
			_impl |= rhs._impl;
			return *this;
		}

		constexpr enum_set
		operator|(const enum_set& rhs) const noexcept
		{
			return enum_set{ _impl | rhs._impl };
		}

		constexpr enum_set
		operator^(const enum_set& rhs) const noexcept
		{
			return enum_set{ _impl ^ rhs._impl };
		}

		constexpr enum_set&
		operator^=(const enum_set& rhs) noexcept
		{
			_impl ^= rhs._impl;
			return *this;
		}

		constexpr enum_set
		operator~() const noexcept
		{
			return enum_set{ ~_impl };
		}

	public:
		[[nodiscard]] explicit constexpr
		operator bool() const noexcept
		{
			return _impl != static_cast<U>(0);
		}

		[[nodiscard]] constexpr E
		operator*() const noexcept
		{
			return get();
		}

		[[nodiscard]] constexpr bool
		empty() const noexcept
		{
			return _impl == static_cast<U>(0);
		}

		[[nodiscard]] constexpr E
		get() const noexcept
		{
			return static_cast<E>(_impl);
		}

		[[nodiscard]] constexpr U
		underlying() const noexcept
		{
			return _impl;
		}

	public:
		template <class... Args>
		constexpr enum_set&
		set(Args... a_args) noexcept
			requires(std::same_as<Args, E> && ...)
		{
			_impl |= (static_cast<U>(a_args) | ...);
			return *this;
		}

		template <class... Args>
		constexpr enum_set&
		set(bool a_set, Args... a_args) noexcept
			requires(std::same_as<Args, E> && ...)
		{
			if (a_set)
				_impl |= (static_cast<U>(a_args) | ...);
			else
				_impl &= ~(static_cast<U>(a_args) | ...);

			return *this;
		}

		template <class... Args>
		constexpr enum_set&
		reset(Args... a_args) noexcept
			requires(std::same_as<Args, E> && ...)
		{
			_impl &= ~(static_cast<U>(a_args) | ...);
			return *this;
		}

		constexpr enum_set&
		reset() noexcept
		{
			_impl = 0;
			return *this;
		}

		template <class... Args>
		[[nodiscard]] constexpr bool
		any(Args... a_args) const noexcept
			requires(std::same_as<Args, E> && ...)
		{
			return (_impl & (static_cast<U>(a_args) | ...)) != static_cast<U>(0);
		}

		template <class... Args>
		[[nodiscard]] constexpr bool
		all(Args... a_args) const noexcept
			requires(std::same_as<Args, E> && ...)
		{
			return (_impl & (static_cast<U>(a_args) | ...)) == (static_cast<U>(a_args) | ...);
		}

		[[nodiscard]] constexpr bool
		all(const enum_set<E, U>& rhs) const noexcept
		{
			return (_impl & rhs._impl) == rhs._impl;
		}

	private:
		U _impl{ 0 };
	};

	template <class... Args>
	enum_set(Args...) -> enum_set<
		std::common_type_t<Args...>,
		std::underlying_type_t<std::common_type_t<Args...>>>;
}
