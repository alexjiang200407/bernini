#pragma once

namespace core::type_traits
{
	template <typename T>
	concept numeric = std::integral<T> || std::floating_point<T>;
}
