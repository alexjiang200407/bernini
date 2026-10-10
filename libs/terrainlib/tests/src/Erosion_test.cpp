#include "erode.h"

#include <algorithm>
#include <assetlib_structs/Heightfield.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <terrainlib/ErosionDesc.h>
#include <terrainlib/Generate.h>
#include <vector>

// What erosion promises: the same desc wears the same field, droplets and thermal erosion move
// ground rather than making or losing it, a plain stays a plain, the pits a noise leaves are joined
// to the edge so its valleys drain, and thermal erosion lays slopes back to their talus angle.

namespace
{
	constexpr uint32_t c_Side = 129;
	constexpr float    c_Cell = 4.0f;

	[[nodiscard]] terrain::ErosionDesc
	Hydraulic()
	{
		return terrain::ErosionDesc().SetDropletsPerSample(1.0f).SetPasses(4);
	}

	/** The noise of a hilly field, unquantised: what erosion runs on inside Generate. */
	[[nodiscard]] std::vector<float>
	Hills(const uint32_t side = c_Side, const uint32_t seed = 5)
	{
		const assetlib::Heightfield field = terrain::Generate(
			terrain::TerrainGenerateDesc()
				.SetSeed(seed)
				.SetSamples(side, side)
				.SetCellSize(c_Cell));
		auto heights = std::vector<float>(field.heights.size());
		for (size_t i = 0; i < heights.size(); ++i)
		{
			heights[i] = field.minHeight +
			             field.heightRange * static_cast<float>(field.heights[i]) / 65535.0f;
		}
		return heights;
	}

	[[nodiscard]] double
	Volume(const std::vector<float>& heights)
	{
		return std::accumulate(heights.begin(), heights.end(), 0.0);
	}

	/** Interior samples lower than all eight neighbours: the pits water would pool in. */
	[[nodiscard]] uint32_t
	Pits(const std::vector<float>& h, const uint32_t side)
	{
		uint32_t pits = 0;
		for (uint32_t z = 1; z + 1 < side; ++z)
		{
			for (uint32_t x = 1; x + 1 < side; ++x)
			{
				const float centre = h[z * side + x];
				bool        lowest = true;
				for (int dz = -1; dz <= 1 && lowest; ++dz)
				{
					for (int dx = -1; dx <= 1; ++dx)
					{
						if ((dx != 0 || dz != 0) && h[(z + static_cast<uint32_t>(dz)) * side + x +
						                              static_cast<uint32_t>(dx)] <= centre)
						{
							lowest = false;
							break;
						}
					}
				}
				pits += lowest ? 1 : 0;
			}
		}
		return pits;
	}

	/** The steepest rise between neighbours along x or z, as rise over run. */
	[[nodiscard]] float
	SteepestSlope(const std::vector<float>& h, const uint32_t side)
	{
		float steepest = 0.0f;
		for (uint32_t z = 0; z < side; ++z)
		{
			for (uint32_t x = 0; x + 1 < side; ++x)
			{
				steepest = std::max(steepest, std::abs(h[z * side + x + 1] - h[z * side + x]));
				steepest = std::max(steepest, std::abs(h[x * side + z] - h[(x + 1) * side + z]));
			}
		}
		return steepest / c_Cell;
	}
}

TEST_CASE(
	"the same desc and seed erode the same field, and another seed another",
	"[terrain][erosion]")
{
	auto a     = Hills();
	auto again = Hills();
	auto b     = Hills();
	terrain::Erode(a, c_Side, c_Side, c_Cell, Hydraulic(), 1);
	terrain::Erode(again, c_Side, c_Side, c_Cell, Hydraulic(), 1);
	terrain::Erode(b, c_Side, c_Side, c_Cell, Hydraulic(), 2);

	CHECK(a == again);
	CHECK(a != b);
	CHECK(a != Hills());
}

TEST_CASE(
	"droplets and thermal erosion move ground and neither make nor lose it",
	"[terrain][erosion]")
{
	auto        heights = Hills();
	const auto  before  = Volume(heights);
	const float range   = *std::ranges::max_element(heights) - *std::ranges::min_element(heights);

	terrain::Erode(
		heights,
		c_Side,
		c_Side,
		c_Cell,
		Hydraulic().SetThermalIterations(8).SetBreachDepth(0.0f),
		3);

	// Float sums over a hundred thousand moves: within a millimetre a sample on average, against
	// a field tens of metres high.
	const double after = Volume(heights);
	INFO("volume " << before << " -> " << after << ", range " << range);
	CHECK(std::abs(after - before) / static_cast<double>(heights.size()) < 1e-3);
}

TEST_CASE("a plain stays a plain", "[terrain][erosion]")
{
	auto heights = std::vector<float>(static_cast<size_t>(c_Side) * c_Side, 12.5f);
	terrain::Erode(heights, c_Side, c_Side, c_Cell, Hydraulic().SetThermalIterations(8), 1);
	CHECK(std::ranges::all_of(heights, [](const float h) { return h == 12.5f; }));
}

TEST_CASE(
	"the pits a noise leaves are joined to the edge, so its valleys drain",
	"[terrain][erosion]")
{
	auto           heights = Hills();
	const uint32_t before  = Pits(heights, c_Side);
	terrain::Erode(heights, c_Side, c_Side, c_Cell, Hydraulic(), 1);
	const uint32_t after = Pits(heights, c_Side);

	INFO("pits " << before << " -> " << after);
	CHECK(after < before / 4);
}

TEST_CASE(
	"a hollow too deep to breach keeps its water, and a shallow one drains",
	"[terrain][erosion]")
{
	// Two bowls in a plain at 30 m: one 5 m deep, one 25 m, and a breach of at most 10 m.
	auto       heights = std::vector<float>(static_cast<size_t>(c_Side) * c_Side, 30.0f);
	const auto bowl    = [&](const float cx, const float depth) {
		for (uint32_t z = 0; z < c_Side; ++z)
		{
			for (uint32_t x = 0; x < c_Side; ++x)
			{
				const float r =
					std::hypot(static_cast<float>(x) - cx, static_cast<float>(z) - 64.0f);
				if (r < 12.0f)
					heights[z * c_Side + x] = 30.0f - depth * (1.0f - r / 12.0f);
			}
		}
	};
	bowl(32.0f, 5.0f);
	bowl(96.0f, 25.0f);
	terrain::BreachBasins(heights, c_Side, c_Side, c_Cell, 10.0f, 0.7f);

	CHECK(Pits(heights, c_Side) == 1);
	CHECK(heights[64 * c_Side + 96] <= 5.0f);
	CHECK(heights[64 * c_Side + 32] == 25.0f);
}

TEST_CASE("thermal erosion lays a slope back toward its talus angle", "[terrain][erosion]")
{
	// A cliff: 40 m up in one cell, far past 30 degrees.
	auto heights = std::vector<float>(static_cast<size_t>(c_Side) * c_Side, 0.0f);
	for (uint32_t z = 0; z < c_Side; ++z)
	{
		for (uint32_t x = c_Side / 2; x < c_Side; ++x)
		{
			heights[z * c_Side + x] = 40.0f;
		}
	}
	const auto before = Volume(heights);

	terrain::Erode(
		heights,
		c_Side,
		c_Side,
		c_Cell,
		terrain::ErosionDesc().SetTalusDegrees(30.0f).SetThermalIterations(200).SetThermalRate(
			1.0f),
		1);

	const float talus = std::tan(30.0f * 3.14159265f / 180.0f);
	INFO("steepest " << SteepestSlope(heights, c_Side) << " against " << talus);
	CHECK(SteepestSlope(heights, c_Side) < talus * 1.25f);
	CHECK(std::abs(Volume(heights) - before) / before < 1e-5);
}

TEST_CASE(
	"Generate erodes as its desc says, and refuses an erosion it cannot run",
	"[terrain][erosion]")
{
	const auto desc =
		terrain::TerrainGenerateDesc().SetSeed(4).SetSamples(65, 65).SetCellSize(4.0f);
	const auto plain  = terrain::Generate(desc);
	const auto eroded = terrain::Generate(
		terrain::TerrainGenerateDesc(desc).SetErosion(Hydraulic().SetThermalIterations(4)));
	CHECK(plain.heights != eroded.heights);
	CHECK(terrain::Generate(desc).heights == plain.heights);

	const auto refuses = [&](const terrain::ErosionDesc& erosion) {
		CHECK_THROWS_AS(
			terrain::Generate(terrain::TerrainGenerateDesc(desc).SetErosion(erosion)),
			std::runtime_error);
	};
	refuses(terrain::ErosionDesc().SetDropletsPerSample(-1.0f));
	refuses(Hydraulic().SetPasses(0));
	refuses(Hydraulic().SetMaxSteps(0));
	refuses(Hydraulic().SetInertia(1.0f));
	refuses(Hydraulic().SetErosionRate(0.0f));
	refuses(Hydraulic().SetDepositionRate(1.5f));
	refuses(Hydraulic().SetEvaporation(1.0f));
	refuses(Hydraulic().SetChannelSteering(2.0f));
	refuses(Hydraulic().SetTalusDegrees(90.0f));
	refuses(Hydraulic().SetThermalRate(0.0f));
}

// Linear in the samples at a fixed density of droplets: sixteen times the samples cost well under
// sixteen times sixteen. A ratio rather than a ceiling, so it holds in a debug build and under load.
TEST_CASE("erosion costs linear in the samples", "[terrain][erosion][perf]")
{
	const auto time = [](const uint32_t side) {
		auto       heights = Hills(side);
		const auto start   = std::chrono::steady_clock::now();
		terrain::Erode(heights, side, side, c_Cell, Hydraulic().SetThermalIterations(4), 1);
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	};

	(void)time(129);
	const double small = time(257);
	const double large = time(1025);
	INFO("257^2 " << small << " s, 1025^2 " << large << " s");
	CHECK(large < 64.0 * std::max(small, 1e-4));
}
