#include <algorithm>
#include <assetlib/grass_patch.h>
#include <assetlib_structs/BGrassFields.h>
#include <assetlib_structs/Grass.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// The patch a preview grows a look on: a field over a square, the same every run, that the renderer
// takes as it takes a cooked one.

using namespace assetlib;

TEST_CASE("A grass patch covers its square with one bound field", "[grass][patch]")
{
	const BGrassFields grass = makeGrassPatch(
		{ .size = 10.0f, .spacing = 0.5f, .seed = 3 },
		"Authored/Grass/verge.bgrass");

	REQUIRE(grass.fields.size() == 1);
	CHECK(grass.names == std::vector<std::string>{ "Patch" });
	CHECK(grass.looks == std::vector<std::string>{ "Authored/Grass/verge.bgrass" });
	CHECK(grass.fields[0].mesh == 0);
	CHECK(grass.fields[0].look == 0);
	CHECK(grass.clumps.size() == 400);

	for (const GrassClump& clump : grass.clumps)
	{
		CHECK(std::abs(clump.position.x) <= 5.0f);
		CHECK(std::abs(clump.position.y) <= 5.0f);
		CHECK(clump.position.z == 0.0f);
		CHECK(clump.normal.z == 1.0f);
	}

	uint32_t chunked = 0;
	for (const GrassChunk& chunk : grass.chunks)
	{
		CHECK(chunk.clumpCount <= c_GrassClumpsPerChunk);
		chunked += chunk.clumpCount;
	}
	CHECK(chunked == grass.clumps.size());
}

TEST_CASE("A grass patch is the same every run, and its seed moves it", "[grass][patch]")
{
	const GrassPatchDesc desc{ .size = 4.0f, .spacing = 0.25f, .seed = 9 };
	const BGrassFields   a = makeGrassPatch(desc, "a.bgrass");
	const BGrassFields   b = makeGrassPatch(desc, "a.bgrass");
	const BGrassFields   c =
		makeGrassPatch({ .size = 4.0f, .spacing = 0.25f, .seed = 10 }, "a.bgrass");

	REQUIRE(a.clumps.size() == b.clumps.size());
	CHECK(std::ranges::equal(a.clumps, b.clumps, [](const GrassClump& x, const GrassClump& y) {
		return x.position == y.position && x.heightScale == y.heightScale;
	}));
	CHECK_FALSE(
		std::ranges::equal(a.clumps, c.clumps, [](const GrassClump& x, const GrassClump& y) {
			return x.position == y.position;
		}));
}

TEST_CASE("A grass patch refuses a square it cannot fill", "[grass][patch]")
{
	CHECK_THROWS(makeGrassPatch({ .size = 0.0f }, "a.bgrass"));
	CHECK_THROWS(makeGrassPatch({ .size = 1.0f, .spacing = -1.0f }, "a.bgrass"));
	CHECK_THROWS(makeGrassPatch({ .size = std::numeric_limits<float>::infinity() }, "a.bgrass"));
	CHECK_THROWS(makeGrassPatch({ .size = 1.0f, .spacing = 2.0f }, "a.bgrass"));
	CHECK_THROWS(makeGrassPatch({ .size = 10000.0f, .spacing = 0.1f }, "a.bgrass"));
}
