#include <assetlib_structs/Heightfield.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <terrainlib/Generate.h>
#include <terrainlib/height.h>

namespace
{
	/**
	 * A 3 x 2 field, a cell of 2 apart, in a range of 10: the near row rises 0, 1/2, 1 along x and
	 * the far row falls 1, 1/2, 0, so x and z are told apart.
	 */
	[[nodiscard]] assetlib::Heightfield
	Ramp()
	{
		auto field        = assetlib::Heightfield();
		field.samplesX    = 3;
		field.samplesZ    = 2;
		field.cellSize    = 2.0f;
		field.minHeight   = -4.0f;
		field.heightRange = 10.0f;
		field.heights     = { 0, 32768, 65535, 65535, 32768, 0 };
		return field;
	}
}

TEST_CASE("a sample reads its own height, above the origin as the renderer lays it", "[terrain]")
{
	const auto field  = Ramp();
	const auto origin = glm::vec3(100.0f, 7.0f, 50.0f);

	CHECK(terrain::HeightAt(field, origin, { 100.0f, 50.0f }) == Catch::Approx(7.0f));
	CHECK(terrain::HeightAt(field, origin, { 104.0f, 50.0f }) == Catch::Approx(17.0f));
	CHECK(terrain::HeightAt(field, origin, { 100.0f, 52.0f }) == Catch::Approx(17.0f));
	CHECK(terrain::HeightAt(field, origin, { 104.0f, 52.0f }) == Catch::Approx(7.0f));
}

TEST_CASE("between samples the height is bilinear", "[terrain]")
{
	const auto field  = Ramp();
	const auto origin = glm::vec3(0.0f);

	// Halfway along x and z between the corners 0, 5, 10 and 5: the mean of the four.
	CHECK(terrain::HeightAt(field, origin, { 1.0f, 1.0f }) == Catch::Approx(5.0f).margin(1e-3));
	CHECK(terrain::HeightAt(field, origin, { 3.0f, 0.0f }) == Catch::Approx(7.5f).margin(1e-3));
	CHECK(terrain::HeightAt(field, origin, { 3.0f, 2.0f }) == Catch::Approx(2.5f).margin(1e-3));
	// A quarter of the way along z at x = 0: from 0 toward 10.
	CHECK(terrain::HeightAt(field, origin, { 0.0f, 0.5f }) == Catch::Approx(2.5f).margin(1e-3));
}

TEST_CASE("past the field's edge the height is the edge's", "[terrain]")
{
	const auto field  = Ramp();
	const auto origin = glm::vec3(0.0f);

	CHECK(terrain::HeightAt(field, origin, { -50.0f, -50.0f }) == Catch::Approx(0.0f));
	CHECK(terrain::HeightAt(field, origin, { 50.0f, -50.0f }) == Catch::Approx(10.0f));
	CHECK(terrain::HeightAt(field, origin, { 50.0f, 50.0f }) == Catch::Approx(0.0f));
}

TEST_CASE("a generated field reads every sample as its own height above the origin", "[terrain]")
{
	const auto field = terrain::Generate(
		terrain::TerrainGenerateDesc().SetSeed(3).SetSamples(65, 65).SetCellSize(4.0f));
	const auto origin = glm::vec3(-128.0f, 2.0f, -128.0f);

	for (uint32_t z = 0; z < field.samplesZ; z += 8)
	{
		for (uint32_t x = 0; x < field.samplesX; x += 8)
		{
			const float sample =
				static_cast<float>(field.heights[static_cast<size_t>(z) * field.samplesX + x]) /
				65535.0f;
			const float height = terrain::HeightAt(
				field,
				origin,
				{ origin.x + static_cast<float>(x) * field.cellSize,
			      origin.z + static_cast<float>(z) * field.cellSize });
			CHECK(height == Catch::Approx(origin.y + sample * field.heightRange).margin(1e-3));
		}
	}
}
