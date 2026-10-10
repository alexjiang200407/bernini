#include "grids.h"

#include <algorithm>
#include <assetlib_structs/Heightfield.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <core/math.h>
#include <core/noise.h>
#include <core/parallel_for.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <terrainlib/fields.h>
#include <terrainlib/layer.h>
#include <terrainlib/masks.h>
#include <vector>

namespace terrain
{
	namespace
	{
		constexpr uint32_t c_ForestSeed = 0x2c1b3c6du;
		constexpr uint32_t c_RockSeed   = 0x297a2d39u;

		// Rounds of correcting the share of samples a mask takes toward its coverage after cleanup.
		constexpr int c_CoverageRounds = 4;

		struct Shape
		{
			uint32_t samplesX;
			uint32_t samplesZ;
			float    cellSize;

			[[nodiscard]] size_t
			Count() const noexcept
			{
				return static_cast<size_t>(samplesX) * samplesZ;
			}
		};

		[[nodiscard]] TerrainLayer
		Empty(const Shape& shape)
		{
			return TerrainLayer{ .samplesX = shape.samplesX,
				                 .samplesZ = shape.samplesZ,
				                 .cellSize = shape.cellSize,
				                 .values   = std::vector<float>(shape.Count(), 0.0f) };
		}

		/** A low-frequency noise over the field, in about [0, 1]. */
		[[nodiscard]] float
		Patch(
			const Shape&   shape,
			const size_t   i,
			const float    patchSize,
			const uint32_t seed) noexcept
		{
			const auto xz = glm::vec2(
				static_cast<float>(i % shape.samplesX) * shape.cellSize,
				static_cast<float>(i / shape.samplesX) * shape.cellSize);
			return 0.5f + 0.5f * core::fbm(xz / patchSize, seed, 3, 2.0f, 0.5f);
		}

		/**
		 * Sets to 1 the samples of `mask` with the highest `score` among those `score` does not mark
		 * ineligible with -infinity, until they number `share` of the field, or all eligible ones do.
		 */
		void
		TakeHighest(std::vector<float>& mask, const std::vector<float>& score, const float share)
		{
			auto eligible = std::vector<float>();
			for (const float s : score)
			{
				if (s > -std::numeric_limits<float>::infinity())
					eligible.push_back(s);
			}
			const auto wanted =
				static_cast<size_t>(std::lround(share * static_cast<float>(score.size())));
			if (wanted == 0 || eligible.empty())
				return;

			float threshold = -std::numeric_limits<float>::infinity();
			if (wanted < eligible.size())
			{
				const auto nth =
					eligible.begin() + static_cast<std::ptrdiff_t>(eligible.size() - wanted);
				std::ranges::nth_element(eligible, nth);
				threshold = *nth;
			}
			for (size_t i = 0; i < score.size(); ++i)
			{
				if (score[i] > -std::numeric_limits<float>::infinity() && score[i] >= threshold)
					mask[i] = 1.0f;
			}
		}

		/**
		 * Flips every group of samples whose mask equals `value`, joined along either axis or
		 * diagonally, smaller than `minSamples`. `keepEdge` keeps a group that touches the field's
		 * edge whatever its size: a gap at the edge is not a clearing, the wood may go on past it.
		 */
		void
		DropSmall(
			std::vector<float>& mask,
			const Shape&        shape,
			const float         value,
			const size_t        minSamples,
			const bool          keepEdge)
		{
			auto seen  = std::vector<bool>(mask.size(), false);
			auto group = std::vector<uint32_t>();
			for (size_t start = 0; start < mask.size(); ++start)
			{
				if (seen[start] || mask[start] != value)
					continue;

				group.clear();
				group.push_back(static_cast<uint32_t>(start));
				seen[start] = true;
				bool edge   = false;
				for (size_t g = 0; g < group.size(); ++g)
				{
					const auto x = static_cast<int>(group[g] % shape.samplesX);
					const auto z = static_cast<int>(group[g] / shape.samplesX);
					edge |= x == 0 || z == 0 || x == static_cast<int>(shape.samplesX) - 1 ||
					        z == static_cast<int>(shape.samplesZ) - 1;
					for (const glm::ivec2 n : core::c_Neighbours8)
					{
						const int nx = x + n.x;
						const int nz = z + n.y;
						if (nx < 0 || nz < 0 || nx >= static_cast<int>(shape.samplesX) ||
						    nz >= static_cast<int>(shape.samplesZ))
							continue;
						const size_t j =
							static_cast<size_t>(nz) * shape.samplesX + static_cast<size_t>(nx);
						if (!seen[j] && mask[j] == value)
						{
							seen[j] = true;
							group.push_back(static_cast<uint32_t>(j));
						}
					}
				}
				if (group.size() < minSamples && !(keepEdge && edge))
				{
					for (const uint32_t i : group) mask[i] = 1.0f - value;
				}
			}
		}

		void
		Validate(const MaskDesc& desc)
		{
			const auto positive = [](const float v) { return std::isfinite(v) && v > 0.0f; };
			const auto share    = [](const float v) {
				return std::isfinite(v) && v >= 0.0f && v <= 1.0f;
			};
			const auto refuse = [](const char* what) {
				core::throw_runtime_error("terrain::GenerateMasks: {} is out of range", what);
			};
			const auto bias = [](const float v) { return std::isfinite(v) && v >= 0.0f; };

			if (!positive(desc.positionRadius))
				refuse("positionRadius");
			if (!positive(desc.positionScale))
				refuse("positionScale");
			if (!share(desc.forest.coverage))
				refuse("forest.coverage");
			if (!positive(desc.forest.patchSize))
				refuse("forest.patchSize");
			if (!positive(desc.forest.maxSlope))
				refuse("forest.maxSlope");
			if (!positive(desc.forest.crestHeight))
				refuse("forest.crestHeight");
			if (!bias(desc.forest.wetnessBias))
				refuse("forest.wetnessBias");
			if (!bias(desc.forest.hollowBias))
				refuse("forest.hollowBias");
			if (!positive(desc.forest.minArea))
				refuse("forest.minArea");
			if (!positive(desc.forest.minClearing))
				refuse("forest.minClearing");
			if (!share(desc.rock.coverage))
				refuse("rock.coverage");
			if (!positive(desc.rock.patchSize))
				refuse("rock.patchSize");
			if (!positive(desc.rock.minSlope))
				refuse("rock.minSlope");
			if (!bias(desc.rock.slopeBias))
				refuse("rock.slopeBias");
			if (!bias(desc.rock.ridgeBias))
				refuse("rock.ridgeBias");
			if (!positive(desc.rock.minArea))
				refuse("rock.minArea");
			if (!positive(desc.water.minLakeDepth))
				refuse("water.minLakeDepth");
			if (!positive(desc.water.riverArea))
				refuse("water.riverArea");
		}
	}

	namespace
	{
		/** GenerateMasks, its water already laid in `masks.water`. */
		void
		GrowOnLand(
			TerrainMasks&                masks,
			const assetlib::Heightfield& field,
			const TerrainFields&         fields,
			const MaskDesc&              desc)
		{
			const auto   shape = Shape{ field.samplesX, field.samplesZ, field.cellSize };
			const size_t count = shape.Count();

			const float sampleArea = shape.cellSize * shape.cellSize;
			const auto  samplesIn  = [&](const float area) {
				return static_cast<size_t>(std::ceil(area / sampleArea));
			};
			constexpr float c_Never = -std::numeric_limits<float>::infinity();

			const std::vector<float>& water  = masks.water.values;
			std::vector<float>&       forest = masks.forest.values;
			std::vector<float>&       rock   = masks.rock.values;

			// Hollow and ridge as a sample's height against the mean around it (the topographic
			// position index), and wetness at the same scale, so a wood follows a valley rather than
			// every gully line up its sides.
			const int radius =
				std::max(static_cast<int>(std::lround(desc.positionRadius / shape.cellSize)), 1);
			auto        position    = std::vector<float>(count);
			const float heightScale = field.heightRange / 65535.0f;
			for (size_t i = 0; i < count; ++i)
				position[i] = static_cast<float>(field.heights[i]) * heightScale;
			const std::vector<float> around =
				BoxMean(position, shape.samplesX, shape.samplesZ, radius);
			for (size_t i = 0; i < count; ++i) position[i] -= around[i];
			const std::vector<float> wetness =
				BoxMean(fields.wetness.values, shape.samplesX, shape.samplesZ, radius);
			const ForestRule& woods = desc.forest;
			auto              score = std::vector<float>(count);
			for (size_t i = 0; i < count; ++i)
			{
				if (water[i] > 0.0f || fields.slope.values[i] > woods.maxSlope ||
				    position[i] > woods.crestHeight)
				{
					score[i] = c_Never;
					continue;
				}
				const float hollow = std::clamp(-position[i] / desc.positionScale, 0.0f, 1.0f);
				score[i]           = Patch(shape, i, woods.patchSize, desc.seed ^ c_ForestSeed) +
				                     woods.wetnessBias * wetness[i] + woods.hollowBias * hollow;
			}
			// Clearings are filled before what a wood may not stand on is taken back out, and only then
			// are the woods too small dropped, so no step puts back what an earlier one ruled out. The
			// cleanup changes how much is covered, so the share taken is corrected toward the rule's
			// coverage over a few rounds: coverage is of the woods as they stand.
			const auto cleanForest = [&](const float share) {
				std::ranges::fill(forest, 0.0f);
				TakeHighest(forest, score, share);
				DropSmall(forest, shape, 0.0f, samplesIn(woods.minClearing), true);
				for (size_t i = 0; i < count; ++i)
				{
					if (score[i] == c_Never)
						forest[i] = 0.0f;
				}
				DropSmall(forest, shape, 1.0f, samplesIn(woods.minArea), false);
				return static_cast<float>(std::ranges::count(forest, 1.0f)) /
				       static_cast<float>(count);
			};
			float share = woods.coverage;
			for (int round = 0; round < c_CoverageRounds && woods.coverage > 0.0f; ++round)
			{
				const float covered = cleanForest(share);
				if (covered <= 0.0f || std::abs(covered - woods.coverage) < 0.005f)
					break;
				share = std::min(share * woods.coverage / covered, 1.0f);
				if (round + 1 == c_CoverageRounds)
					(void)cleanForest(share);
			}
			auto open = std::vector<float>(count);
			for (size_t i = 0; i < count; ++i) open[i] = 1.0f - forest[i];
			const std::vector<float> inside =
				DistanceInside(forest, shape.samplesX, shape.samplesZ, shape.cellSize);
			const std::vector<float> outside =
				DistanceInside(open, shape.samplesX, shape.samplesZ, shape.cellSize);
			for (size_t i = 0; i < count; ++i) masks.forestEdge.values[i] = inside[i] - outside[i];

			const RockRule& rocks = desc.rock;
			for (size_t i = 0; i < count; ++i)
			{
				const float slope = fields.slope.values[i];
				const float ridge = std::clamp(position[i] / desc.positionScale, 0.0f, 1.0f);
				if (water[i] > 0.0f || forest[i] > 0.0f || (slope < rocks.minSlope && ridge < 1.0f))
				{
					score[i] = c_Never;
					continue;
				}
				score[i] = Patch(shape, i, rocks.patchSize, desc.seed ^ c_RockSeed) +
				           rocks.slopeBias * std::min(slope / rocks.minSlope, 2.0f) * 0.5f +
				           rocks.ridgeBias * ridge;
			}
			TakeHighest(rock, score, rocks.coverage);
			DropSmall(rock, shape, 1.0f, samplesIn(rocks.minArea), false);
		}

		[[nodiscard]] TerrainMasks
		EmptyMasks(
			const assetlib::Heightfield& field,
			const TerrainFields&         fields,
			const MaskDesc&              desc)
		{
			Validate(desc);
			const auto shape = Shape{ field.samplesX, field.samplesZ, field.cellSize };
			core::ensure(
				fields.slope.values.size() == shape.Count() &&
					fields.flow.values.size() == shape.Count(),
				"masks of a {} x {} field over fields of {} samples",
				field.samplesX,
				field.samplesZ,
				fields.slope.values.size());
			return TerrainMasks{ .forest     = Empty(shape),
				                 .forestEdge = Empty(shape),
				                 .rock       = Empty(shape),
				                 .water      = Empty(shape) };
		}
	}

	TerrainMasks
	GenerateMasks(
		const assetlib::Heightfield& field,
		const TerrainFields&         fields,
		const MaskDesc&              desc)
	{
		auto masks = EmptyMasks(field, fields, desc);
		for (size_t i = 0; i < masks.water.values.size(); ++i)
		{
			masks.water.values[i] = fields.lakeDepth.values[i] >= desc.water.minLakeDepth ||
			                                fields.flow.values[i] >= desc.water.riverArea ?
			                            1.0f :
			                            0.0f;
		}
		GrowOnLand(masks, field, fields, desc);
		return masks;
	}

	TerrainMasks
	GenerateMasks(
		const assetlib::Heightfield& field,
		const TerrainFields&         fields,
		const MaskDesc&              desc,
		const TerrainLayer&          water)
	{
		auto masks = EmptyMasks(field, fields, desc);
		if (water.values.size() != masks.water.values.size())
		{
			core::throw_runtime_error(
				"terrain::GenerateMasks: a water layer of {} values over a field of {}",
				water.values.size(),
				masks.water.values.size());
		}
		for (size_t i = 0; i < water.values.size(); ++i)
			masks.water.values[i] = water.values[i] > 0.5f ? 1.0f : 0.0f;
		GrowOnLand(masks, field, fields, desc);
		return masks;
	}
}
