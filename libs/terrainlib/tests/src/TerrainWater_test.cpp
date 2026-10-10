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
#include <stdexcept>
#include <terrainlib/Generate.h>
#include <terrainlib/fields.h>
#include <terrainlib/height.h>
#include <terrainlib/layer.h>
#include <terrainlib/masks.h>
#include <terrainlib/types/TerrainLayer.h>
#include <terrainlib/types/TerrainWater.h>
#include <terrainlib/types/WaterDesc.h>
#include <terrainlib/water.h>
#include <vector>

// What carving water promises: a river where enough ground drains, in a channel that holds it, its
// surface never rising toward its mouth and running the way the ground falls; a lake level with the
// lowest point of its rim and dug below it; the shore's distance signed by which side of the edge a
// sample is on; and nothing touched when the desc asks for no water.

namespace
{
	constexpr uint32_t c_Side = 151;
	constexpr float    c_Cell = 2.0f;

	/** A field of `c_Side` samples a side, `height(x, z)` metres at each, x and z in metres. */
	[[nodiscard]] assetlib::Heightfield
	FieldOf(const std::function<float(float, float)>& height)
	{
		auto heights = std::vector<float>();
		for (uint32_t z = 0; z < c_Side; ++z)
		{
			for (uint32_t x = 0; x < c_Side; ++x)
				heights.push_back(
					height(static_cast<float>(x) * c_Cell, static_cast<float>(z) * c_Cell));
		}
		const auto [lowest, highest] = std::ranges::minmax(heights);

		auto field        = assetlib::Heightfield();
		field.samplesX    = c_Side;
		field.samplesZ    = c_Side;
		field.cellSize    = c_Cell;
		field.minHeight   = lowest;
		field.heightRange = highest - lowest;
		for (const float h : heights)
			field.heights.push_back(
				static_cast<uint16_t>(std::lround((h - lowest) / field.heightRange * 65535.0f)));
		return field;
	}

	/** A valley down the middle of the field along z, falling toward z = 0 where it leaves. */
	[[nodiscard]] assetlib::Heightfield
	Valley()
	{
		constexpr float c_Middle = 0.5f * static_cast<float>(c_Side - 1) * c_Cell;
		return FieldOf([](const float x, const float z) {
			return 0.03f * z + 0.15f * std::abs(x - c_Middle);
		});
	}

	/** Rivers from a small catchment up, and no lake. */
	[[nodiscard]] terrain::WaterDesc
	RiversOnly()
	{
		return terrain::WaterDesc()
		    .SetRivers(terrain::RiverRule().SetArea(2.0e4f).SetMinWidth(6.0f).SetMinLength(40.0f))
		    .SetLakes(terrain::LakeRule().SetCount(0));
	}

	[[nodiscard]] float
	At(const terrain::TerrainLayer& layer, const glm::vec2 xz)
	{
		return terrain::LayerAt(layer, glm::vec3(0.0f), xz);
	}

	[[nodiscard]] float
	GroundAt(const assetlib::Heightfield& field, const glm::vec2 xz)
	{
		return terrain::HeightAt(field, glm::vec3(0.0f), xz);
	}
}

TEST_CASE("a valley carries a river whose surface never rises toward its mouth", "[terrain][water]")
{
	auto       field = Valley();
	const auto water = terrain::CarveWater(field, RiversOnly());

	REQUIRE_FALSE(water.rivers.empty());
	const terrain::WaterRiver& river = water.rivers.front();
	REQUIRE(river.course.size() >= 2);
	CHECK(river.course.size() == river.surface.size());
	CHECK(river.course.size() == river.width.size());

	// It runs down the valley: from high z to the edge at z = 0.
	CHECK(river.course.front().y > river.course.back().y);
	CHECK(river.course.back().y == Catch::Approx(0.0f).margin(c_Cell));
	for (size_t p = 1; p < river.surface.size(); ++p)
		CHECK(river.surface[p] <= river.surface[p - 1]);

	// Wet along its middle, and running the way it falls.
	const glm::vec2 middle = river.course[river.course.size() / 2];
	CHECK(At(water.depth, middle) > 0.0f);
	// Read between samples, the surface is off the course by up to the fall over a cell or two.
	CHECK(
		At(water.surface, middle) ==
		Catch::Approx(river.surface[river.course.size() / 2]).margin(0.03f * 2.0f * c_Cell));
	CHECK(At(water.flowZ, middle) < 0.0f);
}

TEST_CASE("a river's banks stand above its surface on either side", "[terrain][water]")
{
	auto       field = Valley();
	const auto desc  = RiversOnly();
	const auto water = terrain::CarveWater(field, desc);
	REQUIRE_FALSE(water.rivers.empty());

	const terrain::WaterRiver& river = water.rivers.front();
	for (size_t p = 1; p + 1 < river.course.size(); p += 5)
	{
		const glm::vec2 along  = glm::normalize(river.course[p + 1] - river.course[p - 1]);
		const glm::vec2 across = glm::vec2(-along.y, along.x);
		const float     reach  = 0.5f * river.width[p] + desc.rivers.bank;
		INFO("point " << p);
		CHECK(GroundAt(field, river.course[p] + across * reach) >= river.surface[p] - 0.01f);
		CHECK(GroundAt(field, river.course[p] - across * reach) >= river.surface[p] - 0.01f);
	}
}

TEST_CASE("a lake stands level at the lowest point of its rim, dug below it", "[terrain][water]")
{
	// A plain tilted gently toward x = 0, flat enough everywhere for a lake.
	auto       field = FieldOf([](const float x, const float z) { return 0.01f * x + 0.002f * z; });
	const auto desc  = terrain::WaterDesc()
	                       .SetRivers(terrain::RiverRule().SetArea(0.0f))
	                       .SetLakes(
							   terrain::LakeRule()
								   .SetCount(1)
								   .SetRadius(30.0f, 30.0f)
								   .SetDepth(3.0f)
								   .SetBank(8.0f));
	const auto water = terrain::CarveWater(field, desc);

	REQUIRE(water.lakes.size() == 1);
	const terrain::WaterLake& lake = water.lakes.front();
	CHECK(At(water.depth, lake.centre) == Catch::Approx(desc.lakes.depth).margin(0.05f));
	CHECK(At(water.shore, lake.centre) < 0.0f);
	CHECK(At(water.flowX, lake.centre) == 0.0f);

	for (size_t i = 0; i < water.depth.values.size(); ++i)
	{
		if (water.depth.values[i] > 0.0f)
			CHECK(water.surface.values[i] == Catch::Approx(lake.level).margin(1e-4f));
	}
	// Nothing is wet past its shore at its widest, banks included.
	const float reach = lake.radius * (1.0f + desc.lakes.shoreNoise) + c_Cell;
	for (size_t i = 0; i < water.depth.values.size(); ++i)
	{
		const glm::vec2 at(
			static_cast<float>(i % c_Side) * c_Cell,
			static_cast<float>(i / c_Side) * c_Cell);
		if (glm::distance(at, lake.centre) > reach)
			CHECK(water.depth.values[i] == 0.0f);
	}
}

TEST_CASE("the shore's distance is positive on land and negative in the water", "[terrain][water]")
{
	auto       field = Valley();
	const auto water = terrain::CarveWater(field, RiversOnly());
	for (size_t i = 0; i < water.shore.values.size(); ++i)
	{
		if (water.depth.values[i] > 0.0f)
			CHECK(water.shore.values[i] < 0.0f);
		else
			CHECK(water.shore.values[i] > 0.0f);
	}
}

TEST_CASE("asking for no water leaves the field as it was and dry", "[terrain][water]")
{
	auto       field  = Valley();
	const auto before = field;
	const auto water  = terrain::CarveWater(
		field,
		terrain::WaterDesc()
			.SetRivers(terrain::RiverRule().SetArea(0.0f))
			.SetLakes(terrain::LakeRule().SetCount(0)));

	CHECK(water.rivers.empty());
	CHECK(water.lakes.empty());
	CHECK(field.heights == before.heights);
	CHECK(field.minHeight == Catch::Approx(before.minHeight));
	CHECK(std::ranges::all_of(water.depth.values, [](const float d) { return d == 0.0f; }));
	CHECK(std::ranges::all_of(water.shore.values, [](const float d) { return d > 0.0f; }));
}

TEST_CASE("the same field and desc carve the same water", "[terrain][water]")
{
	const auto generated = terrain::Generate(
		terrain::TerrainGenerateDesc()
			.SetShape(terrain::TerrainShape::kHilly)
			.SetSeed(3)
			.SetSamples(257, 257));
	const auto desc = terrain::WaterDesc().SetRivers(terrain::RiverRule().SetArea(5.0e4f));

	auto       a     = generated;
	auto       b     = generated;
	const auto first = terrain::CarveWater(a, desc);
	const auto again = terrain::CarveWater(b, desc);
	CHECK(a.heights == b.heights);
	CHECK(first.depth.values == again.depth.values);
	CHECK(first.flowX.values == again.flowX.values);
	CHECK(first.rivers.size() == again.rivers.size());
	CHECK(first.lakes.size() == again.lakes.size());
}

TEST_CASE("masks given a water layer keep everything off it", "[terrain][water]")
{
	auto       field  = Valley();
	const auto water  = terrain::CarveWater(field, RiversOnly());
	const auto fields = terrain::DeriveFields(field);

	auto keepOut = water.shore;
	for (float& v : keepOut.values) v = v < 4.0f ? 1.0f : 0.0f;

	const auto masks = terrain::GenerateMasks(
		field,
		fields,
		terrain::MaskDesc()
			.SetForest(terrain::ForestRule().SetCoverage(0.5f).SetMinArea(200.0f))
			.SetRock(terrain::RockRule().SetCoverage(0.2f).SetMinArea(20.0f)),
		keepOut);
	CHECK(masks.water.values == keepOut.values);
	for (size_t i = 0; i < keepOut.values.size(); ++i)
	{
		if (keepOut.values[i] > 0.0f)
		{
			CHECK(masks.forest.values[i] == 0.0f);
			CHECK(masks.rock.values[i] == 0.0f);
		}
	}

	auto wrong = keepOut;
	wrong.values.pop_back();
	CHECK_THROWS_AS(
		terrain::GenerateMasks(field, fields, terrain::MaskDesc(), wrong),
		std::runtime_error);
}

TEST_CASE("CarveWater refuses water it cannot carve", "[terrain][water]")
{
	auto       field  = Valley();
	const auto refuse = [&](const terrain::WaterDesc& desc) {
		auto copy = field;
		CHECK_THROWS_AS(terrain::CarveWater(copy, desc), std::runtime_error);
	};
	refuse(terrain::WaterDesc().SetRivers(terrain::RiverRule().SetArea(-1.0f)));
	refuse(
		terrain::WaterDesc().SetRivers(terrain::RiverRule().SetMinWidth(30.0f).SetMaxWidth(10.0f)));
	refuse(terrain::WaterDesc().SetRivers(terrain::RiverRule().SetBank(0.0f)));
	refuse(terrain::WaterDesc().SetLakes(terrain::LakeRule().SetRadius(50.0f, 20.0f)));
	refuse(terrain::WaterDesc().SetLakes(terrain::LakeRule().SetShoreNoise(1.0f)));
	refuse(terrain::WaterDesc().SetLakes(terrain::LakeRule().SetDepth(std::nanf(""))));
}
