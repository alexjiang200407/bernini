#include <algorithm>
#include <assetlib_structs/Heightfield.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <terrainlib/ErosionDesc.h>
#include <terrainlib/Generate.h>
#include <terrainlib/TerrainFields.h>
#include <terrainlib/TerrainLayer.h>
#include <terrainlib/TerrainMasks.h>
#include <vector>

// What a mask promises: woods and outcrops big enough to read as one, no gap in a wood too small
// to be a clearing, each wood's depth measured to its own edge, and nothing where its rule forbids
// it -- no wood past the slope limit, nothing on water, no rock in a wood.

namespace
{
	constexpr uint32_t c_Side = 101;
	constexpr float    c_Cell = 2.0f;

	[[nodiscard]] assetlib::Heightfield
	Plain()
	{
		auto field        = assetlib::Heightfield();
		field.samplesX    = c_Side;
		field.samplesZ    = c_Side;
		field.cellSize    = c_Cell;
		field.heightRange = 1.0f;
		field.heights.assign(static_cast<size_t>(c_Side) * c_Side, 0);
		return field;
	}

	/** A plain's fields, but wet wherever `wet(x, z)` says. */
	[[nodiscard]] terrain::TerrainFields
	WetWhere(const std::function<bool(int, int)>& wet)
	{
		auto fields = terrain::DeriveFields(Plain());
		for (int z = 0; z < static_cast<int>(c_Side); ++z)
		{
			for (int x = 0; x < static_cast<int>(c_Side); ++x)
			{
				fields.wetness.values[static_cast<size_t>(z) * c_Side + static_cast<size_t>(x)] =
					wet(x, z) ? 1.0f : 0.0f;
			}
		}
		return fields;
	}

	/** Woods decided by wetness alone, covering what is wet: the noise cannot outvote it. */
	[[nodiscard]] terrain::MaskDesc
	ByWetness(const float coverage)
	{
		// A radius of one sample, so the wetness each test lays out is not averaged away.
		return terrain::MaskDesc().SetPositionRadius(c_Cell).SetForest(
			terrain::ForestRule()
				.SetCoverage(coverage)
				.SetWetnessBias(100.0f)
				.SetMinArea(400.0f)
				.SetMinClearing(200.0f));
	}

	[[nodiscard]] float
	At(const terrain::TerrainLayer& layer, const int x, const int z)
	{
		return layer.values[static_cast<size_t>(z) * layer.samplesX + static_cast<size_t>(x)];
	}

	[[nodiscard]] float
	Share(const terrain::TerrainLayer& layer)
	{
		return std::accumulate(layer.values.begin(), layer.values.end(), 0.0f) /
		       static_cast<float>(layer.values.size());
	}
}

TEST_CASE("a wood smaller than its rule's minimum area is dropped", "[terrain][masks]")
{
	// A 20 x 20-sample wet square (1600 m^2) and a 5 x 5 one (100 m^2), against a minimum of 400.
	const auto wet = [](const int x, const int z) {
		return (x >= 10 && x < 30 && z >= 10 && z < 30) || (x >= 60 && x < 65 && z >= 60 && z < 65);
	};
	const auto masks =
		terrain::GenerateMasks(Plain(), WetWhere(wet), ByWetness(425.0f / (101.0f * 101.0f)));

	CHECK(At(masks.forest, 20, 20) == 1.0f);
	CHECK(At(masks.forest, 62, 62) == 0.0f);
}

TEST_CASE(
	"a gap in a wood smaller than a clearing is filled, a larger one kept",
	"[terrain][masks]")
{
	// A wet 40 x 40 square holding a dry 3 x 3 gap (36 m^2) and a dry 12 x 12 one (576 m^2), against
	// a minimum clearing of 200.
	const auto wet = [](const int x, const int z) {
		const bool square = x >= 10 && x < 50 && z >= 10 && z < 50;
		const bool small  = x >= 15 && x < 18 && z >= 15 && z < 18;
		const bool large  = x >= 30 && x < 42 && z >= 30 && z < 42;
		return square && !small && !large;
	};
	const auto masks =
		terrain::GenerateMasks(Plain(), WetWhere(wet), ByWetness(1447.0f / (101.0f * 101.0f)));

	CHECK(At(masks.forest, 12, 12) == 1.0f);
	CHECK(At(masks.forest, 16, 16) == 1.0f);
	CHECK(At(masks.forest, 36, 36) == 0.0f);
}

TEST_CASE(
	"a wood's edge distance is how deep in it a sample stands, or how far out",
	"[terrain][masks]")
{
	// A disc of 20 samples' radius: 42 m from its centre to the nearest sample outside it, and
	// outside, minus the distance to the nearest sample in it.
	const auto wet = [](const int x, const int z) {
		return std::hypot(static_cast<float>(x) - 50.0f, static_cast<float>(z) - 50.0f) <= 20.0f;
	};
	int disc = 0;
	for (int z = 0; z < static_cast<int>(c_Side); ++z)
		for (int x = 0; x < static_cast<int>(c_Side); ++x) disc += wet(x, z) ? 1 : 0;
	const auto masks = terrain::GenerateMasks(
		Plain(),
		WetWhere(wet),
		ByWetness(static_cast<float>(disc) / static_cast<float>(c_Side * c_Side)));

	CHECK(At(masks.forestEdge, 50, 50) == Catch::Approx(42.0f).margin(c_Cell));
	CHECK(At(masks.forestEdge, 60, 50) == Catch::Approx(22.0f).margin(c_Cell));
	CHECK(At(masks.forestEdge, 75, 50) == Catch::Approx(-10.0f).margin(c_Cell));
	CHECK(
		At(masks.forestEdge, 5, 5) ==
		Catch::Approx(-2.0f * (std::hypot(45.0f, 45.0f) - 20.0f)).margin(c_Cell));
}

TEST_CASE(
	"on eroded ground, woods keep to their slopes and off water, and rock out of woods",
	"[terrain][masks]")
{
	const auto field = terrain::Generate(
		terrain::TerrainGenerateDesc()
			.SetSeed(2)
			.SetSamples(257, 257)
			.SetCellSize(2.0f)
			.SetRelief(1.4f)
			.SetErosion(terrain::ErosionDesc().SetDropletsPerSample(1.0f).SetThermalIterations(4)));
	const auto fields = terrain::DeriveFields(field);
	// A field half a kilometre across drains no 20 ha anywhere, so a smaller river; and it has too
	// little gentle ground off its crests for woods of the default size to cover 15% of it.
	const auto desc  = terrain::MaskDesc()
	                       .SetSeed(3)
	                       .SetForest(terrain::ForestRule().SetCoverage(0.08f))
	                       .SetWater(terrain::WaterRule().SetRiverArea(2.0e4f));
	const auto masks = terrain::GenerateMasks(field, fields, desc);

	for (size_t i = 0; i < masks.forest.values.size(); ++i)
	{
		const float f = masks.forest.values[i];
		const float r = masks.rock.values[i];
		const float w = masks.water.values[i];
		REQUIRE((f == 0.0f || f == 1.0f));
		REQUIRE((r == 0.0f || r == 1.0f));
		if (f > 0.0f)
		{
			CHECK(fields.slope.values[i] <= desc.forest.maxSlope);
			CHECK(w == 0.0f);
		}
		if (r > 0.0f)
		{
			CHECK(f == 0.0f);
			CHECK(w == 0.0f);
		}
	}

	INFO(
		"forest " << Share(masks.forest) << ", rock " << Share(masks.rock) << ", water "
				  << Share(masks.water));
	CHECK(Share(masks.forest) == Catch::Approx(desc.forest.coverage).margin(0.03));
	CHECK(Share(masks.rock) > 0.0f);
	CHECK(Share(masks.water) > 0.0f);

	const auto again = terrain::GenerateMasks(field, fields, desc);
	const auto other = terrain::GenerateMasks(field, fields, terrain::MaskDesc(desc).SetSeed(4));
	CHECK(again.forest.values == masks.forest.values);
	CHECK(again.rock.values == masks.rock.values);
	CHECK(other.forest.values != masks.forest.values);
}

TEST_CASE("GenerateMasks refuses a rule it cannot follow", "[terrain][masks]")
{
	const auto field   = Plain();
	const auto fields  = terrain::DeriveFields(field);
	const auto refuses = [&](const terrain::MaskDesc& desc) {
		CHECK_THROWS_AS(terrain::GenerateMasks(field, fields, desc), std::runtime_error);
	};
	refuses(terrain::MaskDesc().SetPositionRadius(0.0f));
	refuses(terrain::MaskDesc().SetForest(terrain::ForestRule().SetCoverage(1.5f)));
	refuses(terrain::MaskDesc().SetForest(terrain::ForestRule().SetMinArea(-1.0f)));
	refuses(terrain::MaskDesc().SetRock(terrain::RockRule().SetPatchSize(0.0f)));
	refuses(terrain::MaskDesc().SetWater(terrain::WaterRule().SetRiverArea(0.0f)));
}
