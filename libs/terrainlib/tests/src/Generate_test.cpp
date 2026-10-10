#include <algorithm>
#include <assetlib_structs/Heightfield.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <terrainlib/Generate.h>
#include <vector>

// What a generated field promises: the same desc gives the same field, the shapes differ in the
// way their names say, and the samples decode to the heights that were generated.

namespace
{
	/** The height of sample (x, z), in world units. */
	[[nodiscard]] float
	HeightOf(const assetlib::Heightfield& field, const uint32_t x, const uint32_t z)
	{
		const uint16_t sample = field.heights[static_cast<size_t>(z) * field.samplesX + x];
		return field.minHeight + field.heightRange * (static_cast<float>(sample) / 65535.0f);
	}

	/** The mean rise between neighbouring samples along x, as a share of the cell: a slope. */
	[[nodiscard]] float
	MeanSlope(const assetlib::Heightfield& field)
	{
		double sum = 0.0;
		for (uint32_t z = 0; z < field.samplesZ; ++z)
		{
			for (uint32_t x = 1; x < field.samplesX; ++x)
			{
				sum += std::abs(HeightOf(field, x, z) - HeightOf(field, x - 1, z));
			}
		}
		const auto pairs = static_cast<double>(field.samplesZ) * (field.samplesX - 1);
		return static_cast<float>(sum / pairs / field.cellSize);
	}

	terrain::TerrainGenerateDesc
	Small(const terrain::TerrainShape shape, const uint32_t seed = 7)
	{
		return terrain::TerrainGenerateDesc()
		    .SetShape(shape)
		    .SetSeed(seed)
		    .SetSamples(129, 97)
		    .SetCellSize(4.0f);
	}
}

TEST_CASE("the same desc generates the same field, and another seed another", "[terrain]")
{
	const assetlib::Heightfield a     = terrain::Generate(Small(terrain::TerrainShape::kHilly));
	const assetlib::Heightfield again = terrain::Generate(Small(terrain::TerrainShape::kHilly));
	const assetlib::Heightfield b     = terrain::Generate(Small(terrain::TerrainShape::kHilly, 8));

	CHECK(a.samplesX == 129);
	CHECK(a.samplesZ == 97);
	CHECK(a.cellSize == 4.0f);
	CHECK(a.heights.size() == 129u * 97u);

	CHECK(a.heights == again.heights);
	CHECK(a.minHeight == again.minHeight);
	CHECK(a.heightRange == again.heightRange);
	CHECK(a.heights != b.heights);
}

TEST_CASE("a field's samples span its range, which is the relief it holds", "[terrain]")
{
	const assetlib::Heightfield field =
		terrain::Generate(Small(terrain::TerrainShape::kMountainous));

	const auto [low, high] = std::ranges::minmax_element(field.heights);
	CHECK(*low == 0);
	CHECK(*high == 65535);
	CHECK(std::isfinite(field.minHeight));
	CHECK(field.heightRange > 0.0f);

	// Mountains stand a few hundred metres over their valleys; a plain a few metres.
	CHECK(field.heightRange > 100.0f);
	CHECK(field.heightRange < 1000.0f);
	CHECK(terrain::Generate(Small(terrain::TerrainShape::kFlat)).heightRange < 20.0f);
}

TEST_CASE("flat is flatter than hilly is flatter than mountainous", "[terrain]")
{
	const float flat     = MeanSlope(terrain::Generate(Small(terrain::TerrainShape::kFlat)));
	const float hilly    = MeanSlope(terrain::Generate(Small(terrain::TerrainShape::kHilly)));
	const float mountain = MeanSlope(terrain::Generate(Small(terrain::TerrainShape::kMountainous)));

	INFO("mean slopes " << flat << " " << hilly << " " << mountain);
	CHECK(flat < hilly);
	CHECK(hilly < mountain);
	CHECK(flat > 0.0f);
}

TEST_CASE("a finer cell resolves the same land rather than smaller land", "[terrain]")
{
	// Sample (x, z) at 8 m is sample (2x, 2z) at 4 m: the field is a function of world position.
	const assetlib::Heightfield coarse = terrain::Generate(
		Small(terrain::TerrainShape::kHilly).SetSamples(65, 49).SetCellSize(8.0f));
	const assetlib::Heightfield fine = terrain::Generate(Small(terrain::TerrainShape::kHilly));

	float largest = 0.0f;
	for (uint32_t z = 0; z < coarse.samplesZ; ++z)
	{
		for (uint32_t x = 0; x < coarse.samplesX; ++x)
		{
			largest =
				std::max(largest, std::abs(HeightOf(coarse, x, z) - HeightOf(fine, 2 * x, 2 * z)));
		}
	}
	// Each field quantises over its own range, so the two differ by a step of the coarser one.
	CHECK(largest < 2.0f * std::max(coarse.heightRange, fine.heightRange) / 65535.0f + 1e-3f);
}

TEST_CASE("relief scales the shape's heights, and every height with them", "[terrain]")
{
	const assetlib::Heightfield whole = terrain::Generate(Small(terrain::TerrainShape::kHilly));
	const assetlib::Heightfield half =
		terrain::Generate(Small(terrain::TerrainShape::kHilly).SetRelief(0.5f));

	CHECK(half.heightRange == Catch::Approx(whole.heightRange * 0.5f).epsilon(1e-4));
	CHECK(half.minHeight == Catch::Approx(whole.minHeight * 0.5f).epsilon(1e-4));
	CHECK(MeanSlope(half) == Catch::Approx(MeanSlope(whole) * 0.5f).epsilon(1e-3));
}

TEST_CASE("Generate refuses a field it cannot make", "[terrain]")
{
	CHECK_THROWS_AS(
		terrain::Generate(Small(terrain::TerrainShape::kFlat).SetSamples(1, 10)),
		std::runtime_error);
	CHECK_THROWS_AS(
		terrain::Generate(Small(terrain::TerrainShape::kFlat).SetSamples(10, 1)),
		std::runtime_error);
	CHECK_THROWS_AS(
		terrain::Generate(
			Small(terrain::TerrainShape::kFlat).SetSamples(terrain::c_MaxGenerateSamples + 1, 10)),
		std::runtime_error);
	CHECK_THROWS_AS(
		terrain::Generate(Small(terrain::TerrainShape::kFlat).SetCellSize(0.0f)),
		std::runtime_error);
	CHECK_THROWS_AS(
		terrain::Generate(Small(terrain::TerrainShape::kFlat).SetCellSize(-1.0f)),
		std::runtime_error);
	CHECK_THROWS_AS(
		terrain::Generate(Small(terrain::TerrainShape::kFlat).SetRelief(0.0f)),
		std::runtime_error);
	CHECK_THROWS_AS(
		terrain::Generate(
			Small(terrain::TerrainShape::kFlat).SetRelief(std::numeric_limits<float>::infinity())),
		std::runtime_error);
}

// Linear in the samples: sixteen times the samples cost well under sixteen times sixteen. A ratio
// rather than a ceiling, so it holds in a debug build and under load.
TEST_CASE("generation costs linear in the samples", "[terrain][perf]")
{
	const auto time = [](const uint32_t side) {
		const auto desc  = Small(terrain::TerrainShape::kMountainous).SetSamples(side, side);
		const auto start = std::chrono::steady_clock::now();
		const auto field = terrain::Generate(desc);
		const auto end   = std::chrono::steady_clock::now();
		CHECK(field.heights.size() == static_cast<size_t>(side) * side);
		return std::chrono::duration<double>(end - start).count();
	};

	// Once to warm the threads, then the two sizes.
	(void)time(256);
	const double small = time(256);
	const double large = time(1024);
	INFO("256^2 " << small << " s, 1024^2 " << large << " s");
	CHECK(large < 64.0 * std::max(small, 1e-4));
}
