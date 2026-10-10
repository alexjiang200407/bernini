#include "util/WaterSurface.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

namespace bgl::test
{
	namespace
	{
		std::filesystem::path
		SurfaceDir(const char* dirName, const char* surfaceName, std::string_view source)
		{
			const std::filesystem::path dir = std::filesystem::temp_directory_path() / dirName;
			std::filesystem::remove_all(dir);
			std::filesystem::create_directories(dir);

			std::ofstream out(
				dir / (std::string(surfaceName) + ".slang"),
				std::ios::binary | std::ios::trunc);
			REQUIRE(out.is_open());
			out << source;
			out.close();

			std::filesystem::copy_file("./shaders/tests/surfaces/Unlit.slang", dir / "Unlit.slang");
			return dir;
		}
	}

	std::filesystem::path
	WaterSurfaceDir()
	{
		return SurfaceDir("bernini_water_surfaces", "ProbeWater", c_ProbeWater);
	}

	std::filesystem::path
	RefractWaterSurfaceDir()
	{
		return SurfaceDir("bernini_refract_water_surfaces", "RefractWater", c_RefractWater);
	}
}
