#include <assetlib_structs/Heightfield.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstdint>
#include <terrainlib/Generate.h>
#include <terrainlib/height.h>

namespace
{
	/** A 3 x 2 field, a cell of 2 apart, whose samples are 0, 1/2 and 1 of a range of 10 along x. */
	[[nodiscard]] assetlib::Heightfield
	Ramp()
	{
		auto field        = assetlib::Heightfield();
		field.samplesX    = 3;
		field.samplesZ    = 2;
		field.cellSize    = 2.0f;
		field.minHeight   = -4.0f;
		field.heightRange = 10.0f;
		field.heights     = { 0, 32768, 65535, 0, 32768, 65535 };
		return field;
	}
}

TEST_CASE("a sample reads its own height, above the origin as the renderer lays it", "[terrain]")
{
	const auto field  = Ramp();
	const auto origin = glm::vec3(100.0f, 7.0f, 50.0f);

	CHECK(terrain::HeightAt(field, origin, { 100.0f, 50.0f }) == Catch::Approx(7.0f));
	CHECK(terrain::HeightAt(field, origin, { 104.0f, 52.0f }) == Catch::Approx(17.0f));
}

TEST_CASE("between samples the height is bilinear", "[terrain]")
{
	const auto field  = Ramp();
	const auto origin = glm::vec3(0.0f);

	CHECK(terrain::HeightAt(field, origin, { 1.0f, 1.0f }) == Catch::Approx(2.5f).margin(1e-3));
	CHECK(terrain::HeightAt(field, origin, { 3.0f, 0.0f }) == Catch::Approx(7.5f).margin(1e-3));
}

TEST_CASE("past the field's edge the height is the edge's", "[terrain]")
{
	const auto field  = Ramp();
	const auto origin = glm::vec3(0.0f);

	CHECK(terrain::HeightAt(field, origin, { -50.0f, -50.0f }) == Catch::Approx(0.0f));
	CHECK(terrain::HeightAt(field, origin, { 50.0f, 50.0f }) == Catch::Approx(10.0f));
}

TEST_CASE("a generated field's heights span its range above the origin", "[terrain]")
{
	const auto field = terrain::Generate(
		terrain::TerrainGenerateDesc().SetSeed(3).SetSamples(65, 65).SetCellSize(4.0f));
	const auto origin = glm::vec3(-128.0f, 0.0f, -128.0f);

	for (uint32_t z = 0; z < field.samplesZ; z += 8)
	{
		for (uint32_t x = 0; x < field.samplesX; x += 8)
		{
			const float height = terrain::HeightAt(
				field,
				origin,
				{ origin.x + static_cast<float>(x) * field.cellSize,
			      origin.z + static_cast<float>(z) * field.cellSize });
			CHECK(height >= 0.0f);
			CHECK(height <= field.heightRange + 1e-3f);
		}
	}
}
