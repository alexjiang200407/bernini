#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <chrono>
#include <execution>
#include <filesystem>
#include <format>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <source_location>
#include <span>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

using namespace std::string_literals;
using namespace std::literals;

template <typename T>
constexpr size_t
type_tag()
{
	return reinterpret_cast<size_t>(&type_tag<T>);
}
