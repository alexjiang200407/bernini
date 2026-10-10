#include <algorithm>
#include <assetlib_structs/Heightfield.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <terrainlib/Generate.h>
#include <terrainlib/fields.h>
#include <terrainlib/types/ErosionDesc.h>
#include <vector>

// What the fields say about a ground whose shape is known: a plane's slope and no curvature, a
// bowl's hollow and a dome's crest, and water that gathers as it runs downhill.

namespace
{
	constexpr uint32_t c_Side = 65;
	constexpr float    c_Cell = 2.0f;

	/** A field of `height(x, z)` metres at sample (x, z), quantised over its own range. */
	[[nodiscard]] assetlib::Heightfield
	FieldOf(const std::function<float(float, float)>& height, const uint32_t side = c_Side)
	{
		auto values = std::vector<float>();
		for (uint32_t z = 0; z < side; ++z)
		{
			for (uint32_t x = 0; x < side; ++x)
			{
				values.push_back(
					height(static_cast<float>(x) * c_Cell, static_cast<float>(z) * c_Cell));
			}
		}
		const auto [lo, hi] = std::ranges::minmax(values);

		auto field        = assetlib::Heightfield();
		field.samplesX    = side;
		field.samplesZ    = side;
		field.cellSize    = c_Cell;
		field.minHeight   = lo;
		field.heightRange = std::max(hi - lo, 1e-3f);
		for (const float v : values)
		{
			field.heights.push_back(
				static_cast<uint16_t>(std::lround((v - lo) / field.heightRange * 65535.0f)));
		}
		return field;
	}

	[[nodiscard]] float
	At(const terrain::TerrainLayer& layer, const uint32_t x, const uint32_t z)
	{
		return layer.values[static_cast<size_t>(z) * layer.samplesX + x];
	}
}

TEST_CASE("a plane has its own slope everywhere and no curvature", "[terrain][fields]")
{
	// Rising 3 along x and 4 along z per 10: a slope of 0.5.
	const auto field  = FieldOf([](const float x, const float z) { return 0.3f * x + 0.4f * z; });
	const auto fields = terrain::DeriveFields(field);

	for (uint32_t z = 1; z + 1 < c_Side; z += 7)
	{
		for (uint32_t x = 1; x + 1 < c_Side; x += 7)
		{
			CHECK(At(fields.slope, x, z) == Catch::Approx(0.5f).margin(1e-3));
			CHECK(At(fields.curvature, x, z) == Catch::Approx(0.0f).margin(1e-3));
		}
	}
}

TEST_CASE("a bowl curves up and a dome curves down", "[terrain][fields]")
{
	const float mid  = static_cast<float>(c_Side - 1) * c_Cell * 0.5f;
	const auto  bowl = [mid](const float x, const float z) {
		return 0.01f * ((x - mid) * (x - mid) + (z - mid) * (z - mid));
	};
	const auto hollow = terrain::DeriveFields(FieldOf(bowl));
	const auto crest =
		terrain::DeriveFields(FieldOf([&](const float x, const float z) { return -bowl(x, z); }));

	// The Laplacian of 0.01 r^2 is 0.04 everywhere.
	const uint32_t c = c_Side / 2;
	CHECK(At(hollow.curvature, c, c) == Catch::Approx(0.04f).margin(1e-3));
	CHECK(At(crest.curvature, c, c) == Catch::Approx(-0.04f).margin(1e-3));
	CHECK(At(hollow.slope, c, c) == Catch::Approx(0.0f).margin(1e-3));
	CHECK(At(hollow.slope, c + 10, c) == Catch::Approx(0.4f).margin(2e-3));
}

TEST_CASE("a plain has no slope, no curvature and drains nothing", "[terrain][fields]")
{
	const auto fields = terrain::DeriveFields(FieldOf([](float, float) { return 7.0f; }));

	CHECK(std::ranges::all_of(fields.slope.values, [](const float v) { return v == 0.0f; }));
	CHECK(std::ranges::all_of(fields.curvature.values, [](const float v) { return v == 0.0f; }));
	CHECK(std::ranges::all_of(fields.flow.values, [](const float v) {
		return v == c_Cell * c_Cell;
	}));
	CHECK(std::ranges::all_of(fields.wetness.values, [](const float v) { return v == 0.0f; }));
}

TEST_CASE(
	"a hollow holds a lake up to where it spills, and a slope holds none",
	"[terrain][fields]")
{
	// A bowl 6 m deep at its centre, sunk into a plain at 10 m: it fills to the plain.
	const float    mid    = static_cast<float>(c_Side - 1) * c_Cell * 0.5f;
	const auto     fields = terrain::DeriveFields(FieldOf([mid](const float x, const float z) {
		const float r = glm::length(glm::vec2(x - mid, z - mid));
		return r < 20.0f ? 4.0f + 6.0f * (r / 20.0f) * (r / 20.0f) : 10.0f;
	}));
	const uint32_t c      = c_Side / 2;
	CHECK(At(fields.lakeDepth, c, c) == Catch::Approx(6.0f).margin(1e-2));
	CHECK(At(fields.lakeDepth, c + 5, c) == Catch::Approx(6.0f - 6.0f * 0.25f).margin(2e-2));
	CHECK(At(fields.lakeDepth, 2, 2) == 0.0f);

	const auto slope = terrain::DeriveFields(
		FieldOf([](const float x, const float z) { return 0.3f * x + 0.1f * z; }));
	CHECK(std::ranges::all_of(slope.lakeDepth.values, [](const float v) { return v == 0.0f; }));
}

TEST_CASE(
	"water gathers down a valley floor, and the floor is wetter than its sides",
	"[terrain][fields]")
{
	// A V down the middle of x, falling along -z: every side drains into the floor, and the floor
	// runs out at z = 0.
	const float mid    = static_cast<float>(c_Side - 1) * c_Cell * 0.5f;
	const auto  fields = terrain::DeriveFields(FieldOf(
		[mid](const float x, const float z) { return 0.5f * std::abs(x - mid) + 0.1f * z; }));

	const uint32_t floor = c_Side / 2;
	for (uint32_t z = c_Side - 2; z > 1; --z)
	{
		INFO("z " << z);
		CHECK(At(fields.flow, floor, z - 1) > At(fields.flow, floor, z));
	}
	CHECK(At(fields.flow, floor, 1) > 50.0f * At(fields.flow, floor + 20, 1));
	CHECK(At(fields.wetness, floor, 1) > At(fields.wetness, floor + 20, 1));
	// Everything upstream of the outlet drains through it: the whole field but the last row's
	// fringe, which reaches it only in part.
	const float area = static_cast<float>(c_Side * c_Side) * c_Cell * c_Cell;
	CHECK(At(fields.flow, floor, 0) > 0.5f * area);
}

TEST_CASE("on eroded ground, flow grows along every way down", "[terrain][fields]")
{
	const auto field = terrain::Generate(
		terrain::TerrainGenerateDesc().SetSeed(9).SetSamples(129, 129).SetCellSize(4.0f).SetErosion(
			terrain::ErosionDesc().SetDropletsPerSample(1.0f).SetThermalIterations(4)));
	const auto fields = terrain::DeriveFields(field);
	const auto again  = terrain::DeriveFields(field);
	CHECK(fields.flow.values == again.flow.values);

	const auto height = [&](const uint32_t x, const uint32_t z) {
		return field.heights[static_cast<size_t>(z) * field.samplesX + x];
	};

	// From a grid of starts, walk the steepest way down until nothing is lower or the walk reaches
	// the edge, where water leaves: where it ends, more water drains through than where it began.
	uint32_t walks = 0;
	for (uint32_t sz = 8; sz < 128; sz += 16)
	{
		for (uint32_t sx = 8; sx < 128; sx += 16)
		{
			uint32_t x = sx, z = sz, steps = 0;
			for (;;)
			{
				uint32_t bx = x, bz = z;
				for (int dz = -1; dz <= 1; ++dz)
				{
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int nx = static_cast<int>(x) + dx;
						const int nz = static_cast<int>(z) + dz;
						if (nx >= 0 && nz >= 0 && nx < 129 && nz < 129 &&
						    height(static_cast<uint32_t>(nx), static_cast<uint32_t>(nz)) <
						        height(bx, bz))
						{
							bx = static_cast<uint32_t>(nx);
							bz = static_cast<uint32_t>(nz);
						}
					}
				}
				if ((bx == x && bz == z) || bx == 0 || bz == 0 || bx == 128 || bz == 128)
					break;
				x = bx;
				z = bz;
				++steps;
			}
			if (steps >= 8)
			{
				++walks;
				INFO("from " << sx << "," << sz << " to " << x << "," << z);
				CHECK(At(fields.flow, x, z) > 4.0f * At(fields.flow, sx, sz));
			}
		}
	}
	CHECK(walks > 20);
}
