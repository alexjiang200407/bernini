#include <assetlib_structs/Heightfield.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <terrainlib/TerrainFields.h>
#include <terrainlib/TerrainLayer.h>
#include <terrainlib/TerrainMasks.h>
#include <terrainlib/height.h>

namespace
{
	/** The same 3 x 2 ramp height_test reads, as values: 0, 0.5, 1 along x, falling on the far row. */
	[[nodiscard]] terrain::TerrainLayer
	Ramp()
	{
		auto layer     = terrain::TerrainLayer();
		layer.samplesX = 3;
		layer.samplesZ = 2;
		layer.cellSize = 2.0f;
		layer.values   = { 0.0f, 0.5f, 1.0f, 1.0f, 0.5f, 0.0f };
		return layer;
	}
}

TEST_CASE(
	"a layer reads its own value at each sample, bilinear between, the edge's past it",
	"[terrain]")
{
	const auto layer  = Ramp();
	const auto origin = glm::vec3(100.0f, -30.0f, 50.0f);

	CHECK(terrain::LayerAt(layer, origin, { 100.0f, 50.0f }) == Catch::Approx(0.0f));
	CHECK(terrain::LayerAt(layer, origin, { 104.0f, 50.0f }) == Catch::Approx(1.0f));
	CHECK(terrain::LayerAt(layer, origin, { 100.0f, 52.0f }) == Catch::Approx(1.0f));
	CHECK(terrain::LayerAt(layer, origin, { 101.0f, 51.0f }) == Catch::Approx(0.5f));
	CHECK(terrain::LayerAt(layer, origin, { 103.0f, 50.0f }) == Catch::Approx(0.75f));
	CHECK(terrain::LayerAt(layer, origin, { 0.0f, 0.0f }) == Catch::Approx(0.0f));
	CHECK(terrain::LayerAt(layer, origin, { 500.0f, 0.0f }) == Catch::Approx(1.0f));
}

TEST_CASE("a layer stands where the heightfield it was laid like stands", "[terrain]")
{
	// A layer holding a field's own heights reads what HeightAt reads, anywhere: the two lay their
	// samples identically, which is what lets a game sample a field by the position it stood on.
	auto field        = assetlib::Heightfield();
	field.samplesX    = 5;
	field.samplesZ    = 4;
	field.cellSize    = 3.0f;
	field.minHeight   = 0.0f;
	field.heightRange = 1.0f;
	auto layer        = terrain::TerrainLayer{ .samplesX = 5, .samplesZ = 4, .cellSize = 3.0f };
	for (uint32_t i = 0; i < 20; ++i)
	{
		const auto sample = static_cast<uint16_t>((i * 7919u) % 65536u);
		field.heights.push_back(sample);
		layer.values.push_back(static_cast<float>(sample) / 65535.0f);
	}

	const auto origin = glm::vec3(-6.0f, 0.0f, 4.0f);
	for (float z = 0.0f; z < 14.0f; z += 1.3f)
	{
		for (float x = -9.0f; x < 12.0f; x += 1.7f)
		{
			CHECK(
				terrain::LayerAt(layer, origin, { x, z }) ==
				Catch::Approx(terrain::HeightAt(field, origin, { x, z })).margin(1e-5));
		}
	}
}

TEST_CASE("fields and masks are layers a game reads by position", "[terrain]")
{
	// A compiled use of the types every later step shares: built by hand here, by DeriveFields and
	// GenerateMasks once they exist. Nothing here proves a field is right, only that it is laid
	// out to be read.
	const auto one    = terrain::TerrainLayer{ .samplesX = 2,
		                                       .samplesZ = 2,
		                                       .cellSize = 1.0f,
		                                       .values   = { 1.0f, 1.0f, 1.0f, 1.0f } };
	const auto fields = terrain::TerrainFields{ .slope     = one,
		                                        .curvature = one,
		                                        .flow      = one,
		                                        .wetness   = one,
		                                        .lakeDepth = one };
	const auto masks =
		terrain::TerrainMasks{ .forest = one, .forestDepth = one, .rock = one, .water = one };

	CHECK(terrain::LayerAt(fields.wetness, glm::vec3(0.0f), { 0.5f, 0.5f }) == 1.0f);
	CHECK(terrain::LayerAt(masks.forestDepth, glm::vec3(0.0f), { 0.5f, 0.5f }) == 1.0f);
}
