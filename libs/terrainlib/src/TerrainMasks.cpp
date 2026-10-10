#include "neighbours.h"

#include <algorithm>
#include <assetlib_structs/Heightfield.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <core/noise.h>
#include <core/parallel_for.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <terrainlib/TerrainFields.h>
#include <terrainlib/TerrainLayer.h>
#include <terrainlib/TerrainMasks.h>
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

		/** The mean of `values`, a grid of `sx` by `sz`, over the box `r` samples around each, from a summed-area table. */
		[[nodiscard]] std::vector<float>
		BoxMean(const std::vector<float>& values, const uint32_t sx, const uint32_t sz, const int r)
		{
			auto       sums = std::vector<double>(static_cast<size_t>(sx + 1) * (sz + 1), 0.0);
			const auto at   = [sx](const uint32_t x, const uint32_t z) {
				return static_cast<size_t>(z) * (sx + 1) + x;
			};
			for (uint32_t z = 0; z < sz; ++z)
			{
				double row = 0.0;
				for (uint32_t x = 0; x < sx; ++x)
				{
					row += static_cast<double>(values[static_cast<size_t>(z) * sx + x]);
					sums[at(x + 1, z + 1)] = sums[at(x + 1, z)] + row;
				}
			}

			auto mean = std::vector<float>(values.size());
			core::parallel_for(sz, 0, "terrain masks", [&](const size_t zi) {
				const auto z  = static_cast<int>(zi);
				const auto z0 = static_cast<uint32_t>(std::max(z - r, 0));
				const auto z1 = static_cast<uint32_t>(std::min(z + r + 1, static_cast<int>(sz)));
				for (int x = 0; x < static_cast<int>(sx); ++x)
				{
					const auto x0 = static_cast<uint32_t>(std::max(x - r, 0));
					const auto x1 =
						static_cast<uint32_t>(std::min(x + r + 1, static_cast<int>(sx)));
					const double sum =
						sums[at(x1, z1)] - sums[at(x0, z1)] - sums[at(x1, z0)] + sums[at(x0, z0)];
					mean[static_cast<size_t>(z) * sx + static_cast<uint32_t>(x)] =
						static_cast<float>(sum / static_cast<double>((x1 - x0) * (z1 - z0)));
				}
			});
			return mean;
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
					for (const glm::ivec2 n : c_Neighbours)
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

		/**
		 * The squared distance transform of one row (Felzenszwalb and Huttenlocher 2012): for each
		 * i, the least (i - j)^2 + f[j] over j, in place.
		 */
		void
		Transform1D(
			std::vector<float>& f,
			std::vector<float>& d,
			std::vector<int>&   v,
			std::vector<float>& z)
		{
			const auto n = static_cast<int>(f.size());
			int        k = 0;
			v[0]         = 0;
			z[0]         = -std::numeric_limits<float>::infinity();
			z[1]         = std::numeric_limits<float>::infinity();
			for (int q = 1; q < n; ++q)
			{
				for (;;)
				{
					const int   p = v[static_cast<size_t>(k)];
					const float s = ((f[static_cast<size_t>(q)] + static_cast<float>(q * q)) -
					                 (f[static_cast<size_t>(p)] + static_cast<float>(p * p))) /
					                static_cast<float>(2 * q - 2 * p);
					if (s <= z[static_cast<size_t>(k)])
					{
						--k;
						continue;
					}
					++k;
					v[static_cast<size_t>(k)]     = q;
					z[static_cast<size_t>(k)]     = s;
					z[static_cast<size_t>(k) + 1] = std::numeric_limits<float>::infinity();
					break;
				}
			}
			k = 0;
			for (int q = 0; q < n; ++q)
			{
				while (z[static_cast<size_t>(k) + 1] < static_cast<float>(q)) ++k;
				const int p = v[static_cast<size_t>(k)];
				d[static_cast<size_t>(q)] =
					static_cast<float>((q - p) * (q - p)) + f[static_cast<size_t>(p)];
			}
			f.swap(d);
		}

		/** Inside the mask, the distance in metres to the nearest sample outside it; 0 outside. */
		[[nodiscard]] std::vector<float>
		Depth(const std::vector<float>& mask, const Shape& shape)
		{
			// Far enough to stand for no sample outside at all, small enough to square in a float.
			constexpr float c_Far = 1.0e12f;

			const uint32_t sx   = shape.samplesX;
			const uint32_t sz   = shape.samplesZ;
			auto           grid = std::vector<float>(mask.size());
			for (size_t i = 0; i < mask.size(); ++i) grid[i] = mask[i] > 0.5f ? c_Far : 0.0f;

			const auto longest = std::max(sx, sz);
			auto       f       = std::vector<float>(longest);
			auto       d       = std::vector<float>(longest);
			auto       v       = std::vector<int>(longest);
			auto       zs      = std::vector<float>(longest + 1);
			for (uint32_t z = 0; z < sz; ++z)
			{
				f.resize(sx);
				d.resize(sx);
				std::copy_n(grid.begin() + static_cast<std::ptrdiff_t>(z) * sx, sx, f.begin());
				Transform1D(f, d, v, zs);
				std::ranges::copy(f, grid.begin() + static_cast<std::ptrdiff_t>(z) * sx);
			}
			for (uint32_t x = 0; x < sx; ++x)
			{
				f.resize(sz);
				d.resize(sz);
				for (uint32_t z = 0; z < sz; ++z) f[z] = grid[static_cast<size_t>(z) * sx + x];
				Transform1D(f, d, v, zs);
				for (uint32_t z = 0; z < sz; ++z) grid[static_cast<size_t>(z) * sx + x] = f[z];
			}
			for (float& g : grid) g = std::sqrt(g) * shape.cellSize;
			return grid;
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

	TerrainMasks
	GenerateMasks(
		const assetlib::Heightfield& field,
		const TerrainFields&         fields,
		const MaskDesc&              desc)
	{
		Validate(desc);
		const auto   shape = Shape{ field.samplesX, field.samplesZ, field.cellSize };
		const size_t count = shape.Count();
		core::ensure(
			fields.slope.values.size() == count && fields.flow.values.size() == count,
			"masks of a {} x {} field over fields of {} samples",
			field.samplesX,
			field.samplesZ,
			fields.slope.values.size());

		const float sampleArea = shape.cellSize * shape.cellSize;
		const auto  samplesIn  = [&](const float area) {
			return static_cast<size_t>(std::ceil(area / sampleArea));
		};
		constexpr float c_Never = -std::numeric_limits<float>::infinity();

		auto                masks  = TerrainMasks{ .forest      = Empty(shape),
			                                       .forestDepth = Empty(shape),
			                                       .rock        = Empty(shape),
			                                       .water       = Empty(shape) };
		std::vector<float>& water  = masks.water.values;
		std::vector<float>& forest = masks.forest.values;
		std::vector<float>& rock   = masks.rock.values;

		for (size_t i = 0; i < count; ++i)
		{
			water[i] = fields.lakeDepth.values[i] >= desc.water.minLakeDepth ||
			                   fields.flow.values[i] >= desc.water.riverArea ?
			               1.0f :
			               0.0f;
		}

		// Hollow and ridge as a sample's height against the mean around it (the topographic
		// position index), and wetness at the same scale, so a wood follows a valley rather than
		// every gully line up its sides.
		const int radius =
			std::max(static_cast<int>(std::lround(desc.positionRadius / shape.cellSize)), 1);
		auto        position    = std::vector<float>(count);
		const float heightScale = field.heightRange / 65535.0f;
		for (size_t i = 0; i < count; ++i)
			position[i] = static_cast<float>(field.heights[i]) * heightScale;
		const std::vector<float> around = BoxMean(position, shape.samplesX, shape.samplesZ, radius);
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
			return static_cast<float>(std::ranges::count(forest, 1.0f)) / static_cast<float>(count);
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
		masks.forestDepth.values = Depth(forest, shape);

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
		return masks;
	}
}
