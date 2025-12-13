#pragma once

namespace core::type_traits
{
	template <typename T>
	concept numeric = std::integral<T> || std::floating_point<T>;

	template <typename T>
	concept trivially_copyable = std::is_trivially_copyable_v<T>;
}
