#include "util/WaterSurface.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <ios>

namespace bgl::test
{
	std::filesystem::path
	WaterSurfaceDir()
	{
		const std::filesystem::path dir =
			std::filesystem::temp_directory_path() / "bernini_water_surfaces";
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);

		std::ofstream out(dir / "ProbeWater.slang", std::ios::binary | std::ios::trunc);
		REQUIRE(out.is_open());
		out << c_ProbeWater;
		out.close();

		std::filesystem::copy_file("./shaders/tests/surfaces/Unlit.slang", dir / "Unlit.slang");
		return dir;
	}
}
