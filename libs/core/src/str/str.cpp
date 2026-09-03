#include <core/str/str.h>

namespace core::str
{
	std::string
	format_bytes(const uint64_t bytes)
	{
		constexpr std::array<const char*, 5> c_Units = { { "B", "KiB", "MiB", "GiB", "TiB" } };

		auto        value = static_cast<double>(bytes);
		std::size_t unit  = 0;
		while (value >= 1024.0 && unit + 1 < c_Units.size())
		{
			value /= 1024.0;
			++unit;
		}

		return unit == 0 ? std::format("{:.0f} {}", value, c_Units[unit]) :
		                   std::format("{:.1f} {}", value, c_Units[unit]);
	}
}
