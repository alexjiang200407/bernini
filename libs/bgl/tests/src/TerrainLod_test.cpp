#include "scene/terrain_lod.h"
#include <algorithm>
#include <array>
#include <bgl/glm.h>
#include <bgl/idl/Constants.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

// The terrain's level-of-detail rule, on the CPU twin the stage's amplification shader follows:
// how a field is cut into nodes, and that the rule covers it exactly once, with no two neighbours
// more than a level apart. No device: the rule is arithmetic.

namespace
{
	struct Field
	{
		uint32_t samplesX;
		uint32_t samplesZ;
		float    cellSize;
		uint32_t levels;
	};

	Field
	MakeField(const uint32_t samplesX, const uint32_t samplesZ, const float cellSize)
	{
		return { samplesX, samplesZ, cellSize, bgl::TerrainLevels(samplesX, samplesZ) };
	}

	/** The nearest distance from `camera` to a node's bounds, its heights taken as flat at 0. */
	float
	NearestDistance(const Field& field, const bgl::TerrainNode& node, const glm::vec3& camera)
	{
		const float cells = static_cast<float>(bgl::TerrainNodeCells(node.level));
		const float x0    = static_cast<float>(node.x) * cells * field.cellSize;
		const float z0    = static_cast<float>(node.z) * cells * field.cellSize;
		const float x1    = std::min(
			x0 + cells * field.cellSize,
			static_cast<float>(field.samplesX - 1) * field.cellSize);
		const float z1 = std::min(
			z0 + cells * field.cellSize,
			static_cast<float>(field.samplesZ - 1) * field.cellSize);

		const float dx = std::max({ x0 - camera.x, 0.0f, camera.x - x1 });
		const float dz = std::max({ z0 - camera.z, 0.0f, camera.z - z1 });
		return std::sqrt(dx * dx + camera.y * camera.y + dz * dz);
	}

	std::vector<float>
	Ranges(const Field& field, const float pixelsPerUnit, const float pixelsPerCell)
	{
		auto ranges = std::vector<float>(field.levels);
		for (uint32_t level = 0; level < field.levels; ++level)
		{
			ranges[level] =
				bgl::TerrainLevelRange(level, field.cellSize, pixelsPerUnit, pixelsPerCell);
		}
		return ranges;
	}

	bool
	Draws(
		const Field&              field,
		const bgl::TerrainNode&   node,
		const glm::vec3&          camera,
		const std::vector<float>& ranges)
	{
		const bgl::TerrainNode parent{ node.level + 1, node.x / 2, node.z / 2 };
		const float            distance = NearestDistance(field, node, camera);
		const float            parentDistance =
			node.level + 1 < field.levels ? NearestDistance(field, parent, camera) : 0.0f;
		return bgl::TerrainNodeDraws(
			node.level,
			field.levels,
			distance,
			parentDistance,
			ranges.data());
	}

	/** The level each level-0 node is drawn at, by the one node of its chain that draws. */
	std::vector<uint32_t>
	DrawnLevels(const Field& field, const glm::vec3& camera, const std::vector<float>& ranges)
	{
		const uint32_t acrossX = bgl::TerrainNodesAcross(field.samplesX, 0);
		const uint32_t acrossZ = bgl::TerrainNodesAcross(field.samplesZ, 0);
		auto           drawn   = std::vector<uint32_t>(acrossX * acrossZ, field.levels);

		for (uint32_t z = 0; z < acrossZ; ++z)
		{
			for (uint32_t x = 0; x < acrossX; ++x)
			{
				uint32_t covered = 0;
				for (uint32_t level = 0; level < field.levels; ++level)
				{
					const bgl::TerrainNode node{ level, x >> level, z >> level };
					if (Draws(field, node, camera, ranges))
					{
						++covered;
						drawn[z * acrossX + x] = level;
					}
				}
				INFO(
					"finest node " << x << ", " << z << " at camera " << camera.x << ", "
								   << camera.y << ", " << camera.z);
				CHECK(covered == 1);
			}
		}
		return drawn;
	}
}

TEST_CASE("a field is cut into levels until one node spans it", "[terrain][lod]")
{
	// 16 samples are 15 cells: three nodes of 7 across, then two of 14, then one of 28.
	const Field small = MakeField(16, 16, 1.0f);
	CHECK(bgl::TerrainNodesAcross(16, 0) == 3);
	CHECK(bgl::TerrainNodesAcross(16, 1) == 2);
	CHECK(bgl::TerrainNodesAcross(16, 2) == 1);
	CHECK(small.levels == 3);
	CHECK(bgl::TerrainNodeCount(16, 16, small.levels) == 9 + 4 + 1);

	// Two samples are one cell: one node, one level.
	CHECK(bgl::TerrainLevels(2, 2) == 1);
	CHECK(bgl::TerrainNodeCount(2, 2, 1) == 1);

	// A field as wide as the ceiling still fits the level count the IDL allows.
	CHECK(bgl::TerrainLevels(bgl::idl::cTerrainMaxSamples, 2) <= bgl::idl::cTerrainMaxLevels);
	CHECK(
		bgl::TerrainNodesAcross(
			bgl::idl::cTerrainMaxSamples,
			bgl::TerrainLevels(bgl::idl::cTerrainMaxSamples, 2) - 1) == 1);

	// The level-major layout round-trips through TerrainNodeAt.
	const Field wide  = MakeField(201, 151, 2.0f);
	uint32_t    index = 0;
	for (uint32_t level = 0; level < wide.levels; ++level)
	{
		const uint32_t acrossX = bgl::TerrainNodesAcross(201, level);
		const uint32_t acrossZ = bgl::TerrainNodesAcross(151, level);
		for (uint32_t z = 0; z < acrossZ; ++z)
		{
			for (uint32_t x = 0; x < acrossX; ++x)
			{
				const bgl::TerrainNode node = bgl::TerrainNodeAt(201, 151, wide.levels, index);
				CHECK(node.level == level);
				CHECK(node.x == x);
				CHECK(node.z == z);
				++index;
			}
		}
	}
	CHECK(index == bgl::TerrainNodeCount(201, 151, wide.levels));
}

TEST_CASE("the level rule covers a field exactly once, from anywhere", "[terrain][lod]")
{
	// A 400 x 300 m field at 300 pixels per unit: level 0 reaches 100 m, so a camera on the field
	// sees several levels at once.
	const Field              field  = MakeField(201, 151, 2.0f);
	const std::vector<float> ranges = Ranges(field, 300.0f, 6.0f);
	REQUIRE(field.levels >= 3);

	// Ranges grow with the level, and each is at least a node's diagonal.
	for (uint32_t level = 0; level + 1 < field.levels; ++level)
	{
		CHECK(ranges[level] < ranges[level + 1]);
		CHECK(
			ranges[level] >=
			1.4142f * static_cast<float>(bgl::TerrainNodeCells(level)) * field.cellSize);
	}

	const std::array<glm::vec3, 6> cameras = {
		glm::vec3(200.0f, 30.0f, 150.0f),     // over the middle
		glm::vec3(0.0f, 2.0f, 0.0f),          // on a corner, at eye height
		glm::vec3(400.0f, 10.0f, 300.0f),     // the far corner
		glm::vec3(-500.0f, 50.0f, 150.0f),    // off the field's edge
		glm::vec3(200.0f, 3000.0f, 150.0f),   // far above: everything coarse
		glm::vec3(1000.0f, 20.0f, -1000.0f),  // far away: everything coarse
	};

	for (const glm::vec3& camera : cameras)
	{
		const std::vector<uint32_t> drawn = DrawnLevels(field, camera, ranges);

		// Every finest node found its one level (DrawnLevels checks the count), and the levels
		// seen span the field: the near ground is fine where the camera stands over it.
		const auto [low, high] = std::ranges::minmax_element(drawn);
		if (camera.y < 100.0f && camera.x >= 0.0f && camera.x <= 400.0f)
		{
			CHECK(*low == 0);
			CHECK(*high > 0);
		}
		else
		{
			CHECK(*low > 0);
		}

		// No two neighbouring patches differ by more than one level.
		const uint32_t acrossX = bgl::TerrainNodesAcross(field.samplesX, 0);
		const uint32_t acrossZ = bgl::TerrainNodesAcross(field.samplesZ, 0);
		for (uint32_t z = 0; z < acrossZ; ++z)
		{
			for (uint32_t x = 0; x < acrossX; ++x)
			{
				const uint32_t here = drawn[z * acrossX + x];
				if (x + 1 < acrossX)
				{
					const uint32_t right = drawn[z * acrossX + x + 1];
					CHECK(std::max(here, right) - std::min(here, right) <= 1);
				}
				if (z + 1 < acrossZ)
				{
					const uint32_t down = drawn[(z + 1) * acrossX + x];
					CHECK(std::max(here, down) - std::min(here, down) <= 1);
				}
			}
		}
	}
}
