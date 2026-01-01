#pragma once
namespace core
{
	template <class E, class U = std::underlying_type_t<E>>
	class EnumSet
	{
	public:
		using EnumType       = E;
		using UnderlyingType = U;

		static_assert(std::is_enum_v<E>, "EnumSet<E, ...> must be an enum");
		static_assert(std::is_integral_v<U>, "EnumSet<..., U> must be an integral");

		constexpr EnumSet() noexcept               = default;
		constexpr EnumSet(const EnumSet&) noexcept = default;
		constexpr EnumSet(EnumSet&&) noexcept      = default;
		explicit constexpr EnumSet(U a_value) noexcept : _impl(a_value) {}

		template <class U2>
		constexpr EnumSet(EnumSet<E, U2> a_rhs) noexcept : _impl(static_cast<U>(a_rhs.Get()))
		{}

		template <class... Args>
		constexpr EnumSet(Args... a_values) noexcept
			requires(std::same_as<Args, E> && ...)
			: _impl((static_cast<U>(a_values) | ...))
		{}

		~EnumSet() noexcept = default;

		constexpr EnumSet&
		operator=(const EnumSet&) noexcept = default;
		constexpr EnumSet&
		operator=(EnumSet&&) noexcept = default;

		template <class U2>
		constexpr EnumSet&
		operator=(EnumSet<E, U2> a_rhs) noexcept
		{
			_impl = static_cast<U>(a_rhs.Get());
			return *this;
		}

		constexpr EnumSet&
		operator=(E a_value) noexcept
		{
			_impl = static_cast<U>(a_value);
			return *this;
		}

		constexpr EnumSet&
		operator&=(const EnumSet& rhs) noexcept
		{
			_impl &= rhs._impl;
			return *this;
		}

		constexpr EnumSet
		operator&(const EnumSet& rhs) const noexcept
		{
			return EnumSet{ _impl & rhs._impl };
		}

		constexpr EnumSet&
		operator|=(const EnumSet& rhs) noexcept
		{
			_impl |= rhs._impl;
			return *this;
		}

		constexpr EnumSet
		operator|(const EnumSet& rhs) const noexcept
		{
			return EnumSet{ _impl | rhs._impl };
		}

		constexpr EnumSet
		operator^(const EnumSet& rhs) const noexcept
		{
			return EnumSet{ _impl ^ rhs._impl };
		}

		constexpr EnumSet&
		operator^=(const EnumSet& rhs) noexcept
		{
			_impl ^= rhs._impl;
			return *this;
		}

		constexpr EnumSet
		operator~() const noexcept
		{
			return EnumSet{ ~_impl };
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
			return Get();
		}

		[[nodiscard]] constexpr E
		Get() const noexcept
		{
			return static_cast<E>(_impl);
		}

		[[nodiscard]] constexpr U
		Underlying() const noexcept
		{
			return _impl;
		}

	public:
		template <class... Args>
		constexpr EnumSet&
		Set(Args... a_args) noexcept
			requires(std::same_as<Args, E> && ...)
		{
			_impl |= (static_cast<U>(a_args) | ...);
			return *this;
		}

		template <class... Args>
		constexpr EnumSet&
		Set(bool a_set, Args... a_args) noexcept
			requires(std::same_as<Args, E> && ...)
		{
			if (a_set)
				_impl |= (static_cast<U>(a_args) | ...);
			else
				_impl &= ~(static_cast<U>(a_args) | ...);

			return *this;
		}

		template <class... Args>
		constexpr EnumSet&
		Reset(Args... a_args) noexcept
			requires(std::same_as<Args, E> && ...)
		{
			_impl &= ~(static_cast<U>(a_args) | ...);
			return *this;
		}

		constexpr EnumSet&
		Reset() noexcept
		{
			_impl = 0;
			return *this;
		}

		template <class... Args>
		[[nodiscard]] constexpr bool
		Any(Args... a_args) const noexcept
			requires(std::same_as<Args, E> && ...)
		{
			return (_impl & (static_cast<U>(a_args) | ...)) != static_cast<U>(0);
		}

		template <class... Args>
		[[nodiscard]] constexpr bool
		All(Args... a_args) const noexcept
			requires(std::same_as<Args, E> && ...)
		{
			return (_impl & (static_cast<U>(a_args) | ...)) == (static_cast<U>(a_args) | ...);
		}

		[[nodiscard]] constexpr bool
		All(const EnumSet<E, U>& rhs) const noexcept
		{
			return (_impl & rhs._impl) == rhs._impl;
		}

	private:
		U _impl{ 0 };
	};

	template <class... Args>
	EnumSet(Args...) -> EnumSet<
		std::common_type_t<Args...>,
		std::underlying_type_t<std::common_type_t<Args...>>>;
}
