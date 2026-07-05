#pragma once

namespace core::type_traits
{
	template <typename T>
	concept numeric = std::integral<T> || std::floating_point<T>;

	template <typename T>
	concept trivially_copyable = std::is_trivially_copyable_v<T>;

	template <typename T>
	concept default_constructible = std::is_default_constructible_v<T>;

	template <typename T>
	concept is_nothrow_copy_assignable = std::is_nothrow_copy_assignable_v<T>;

	template <typename T>
	concept is_nothrow_destructible = std::is_nothrow_destructible_v<T>;
}
