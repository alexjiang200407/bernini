#include <array>
#include <core/str/str.h>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace core::str
{
	std::string
	format_bytes(const uint64_t bytes)
	{
		constexpr std::array<const char*, 5> c_Units      = { { "B", "KiB", "MiB", "GiB", "TiB" } };
		constexpr auto                       c_UnitStride = 1024.0;

		auto        value = static_cast<double>(bytes);
		std::size_t unit  = 0;
		while (value >= c_UnitStride && unit + 1 < c_Units.size())
		{
			value /= c_UnitStride;
			++unit;
		}

		return unit == 0 ? std::format("{:.0f} {}", value, c_Units[unit]) :
		                   std::format("{:.1f} {}", value, c_Units[unit]);
	}
}
