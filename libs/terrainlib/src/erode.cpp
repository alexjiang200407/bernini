#include "erode.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <core/hash.h>
#include <core/math.h>
#include <core/parallel_for.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <span>
#include <terrainlib/types/ErosionDesc.h>
#include <utility>
#include <vector>

// Particle hydraulic erosion after SimpleHydrology (McDonald) and Lague's write-up of Olsen 2004,
// and thermal erosion after Olsen. docs/terrain.md § Erosion says what each step means.

namespace terrain
{
	namespace
	{
		// Water a channel must have carried, in droplets through a cell per droplet released per
		// sample, before it reads as worn: the scale of SimpleHydrology's erf(0.4 * discharge).
		constexpr float c_ChannelWater = 8.0f;

		// The fall, rise over run, a breach is cut with toward the edge: enough that water in it
		// knows which way to go.
		constexpr float c_BasinSlope = 1e-3f;

		// The banks of a breach, rise over run: gentler than any talus angle, so the way a whole basin
		// drains reads as a valley rather than a gorge.
		constexpr float c_BreachBank = 0.25f;

		// How much of a pass's water a channel's memory takes in, against what earlier passes left.
		constexpr float c_ChannelMemory = 0.5f;

		struct Grid
		{
			std::span<float> heights;
			uint32_t         samplesX;
			uint32_t         samplesZ;

			[[nodiscard]] size_t
			Index(const uint32_t x, const uint32_t z) const noexcept
			{
				return static_cast<size_t>(z) * samplesX + x;
			}
		};

		struct HeightAndGradient
		{
			float     height;
			glm::vec2 gradient;  // metres per cell
		};

		/** The height and gradient at `pos`, in cells, bilinear over the cell it lies in. */
		[[nodiscard]] HeightAndGradient
		Sample(const Grid& grid, const glm::vec2 pos) noexcept
		{
			const auto  x  = static_cast<uint32_t>(pos.x);
			const auto  z  = static_cast<uint32_t>(pos.y);
			const float tx = pos.x - static_cast<float>(x);
			const float tz = pos.y - static_cast<float>(z);

			const float h00 = grid.heights[grid.Index(x, z)];
			const float h10 = grid.heights[grid.Index(x + 1, z)];
			const float h01 = grid.heights[grid.Index(x, z + 1)];
			const float h11 = grid.heights[grid.Index(x + 1, z + 1)];

			return { .height   = glm::mix(glm::mix(h00, h10, tx), glm::mix(h01, h11, tx), tz),
				     .gradient = glm::vec2(
						 (h10 - h00) * (1.0f - tz) + (h11 - h01) * tz,
						 (h01 - h00) * (1.0f - tx) + (h11 - h10) * tx) };
		}

		/** Adds `amount` metres at `pos`, shared over the cell's four corners by their weights. */
		void
		Spread(const Grid& grid, const glm::vec2 pos, const float amount) noexcept
		{
			const auto  x  = static_cast<uint32_t>(pos.x);
			const auto  z  = static_cast<uint32_t>(pos.y);
			const float tx = pos.x - static_cast<float>(x);
			const float tz = pos.y - static_cast<float>(z);

			grid.heights[grid.Index(x, z)] += amount * (1.0f - tx) * (1.0f - tz);
			grid.heights[grid.Index(x + 1, z)] += amount * tx * (1.0f - tz);
			grid.heights[grid.Index(x, z + 1)] += amount * (1.0f - tx) * tz;
			grid.heights[grid.Index(x + 1, z + 1)] += amount * tx * tz;
		}

		/** The samples a droplet wears from around the one it stands on, and each one's share. */
		struct Brush
		{
			std::vector<glm::ivec2> offsets;
			std::vector<float>      weights;
			uint32_t                reach = 0;  // cells, along either axis
		};

		[[nodiscard]] Brush
		MakeBrush(const float radius, const float cellSize)
		{
			const float cells = std::max(radius / cellSize, 1.0f);
			auto        brush = Brush{ .reach = static_cast<uint32_t>(std::ceil(cells)) };
			const auto  reach = static_cast<int>(brush.reach);
			float       total = 0.0f;
			for (int dz = -reach; dz <= reach; ++dz)
			{
				for (int dx = -reach; dx <= reach; ++dx)
				{
					const float weight = cells - glm::length(glm::vec2(dx, dz));
					if (weight > 0.0f)
					{
						brush.offsets.emplace_back(dx, dz);
						brush.weights.push_back(weight);
						total += weight;
					}
				}
			}
			for (float& w : brush.weights)
			{
				w /= total;
			}
			return brush;
		}

		/**
		 * Takes `amount` metres from around `node`, shared by the brush's weights over the samples
		 * of it inside the field, renormalised so the whole amount is taken at the edge too.
		 */
		void
		Wear(
			const Grid&      grid,
			const Brush&     brush,
			const glm::uvec2 node,
			const float      amount) noexcept
		{
			float inside = 0.0f;
			for (size_t b = 0; b < brush.offsets.size(); ++b)
			{
				const glm::ivec2 at = glm::ivec2(node) + brush.offsets[b];
				if (at.x >= 0 && at.y >= 0 && at.x < static_cast<int>(grid.samplesX) &&
				    at.y < static_cast<int>(grid.samplesZ))
				{
					inside += brush.weights[b];
				}
			}
			for (size_t b = 0; b < brush.offsets.size(); ++b)
			{
				const glm::ivec2 at = glm::ivec2(node) + brush.offsets[b];
				if (at.x >= 0 && at.y >= 0 && at.x < static_cast<int>(grid.samplesX) &&
				    at.y < static_cast<int>(grid.samplesZ))
				{
					grid.heights[grid.Index(
						static_cast<uint32_t>(at.x),
						static_cast<uint32_t>(at.y))] -= amount * brush.weights[b] / inside;
				}
			}
		}

		/** A small counter-based random stream: the n-th value of a key, in [0, 1). */
		struct Random
		{
			uint32_t key;
			uint32_t next = 0;

			[[nodiscard]] float
			Next() noexcept
			{
				const uint32_t bits = core::hash_mix32(key ^ core::hash_mix32(next++));
				return static_cast<float>(bits >> 8) * (1.0f / 16777216.0f);
			}
		};

		/** Where water ran in earlier passes, per sample: how much, and which way on average. */
		struct Channels
		{
			std::vector<float>     discharge;
			std::vector<glm::vec2> momentum;

			// This pass's track, folded into the two above when it ends.
			std::vector<float>     dischargeTrack;
			std::vector<glm::vec2> momentumTrack;
		};

		/** One droplet from `pos` on, eroding and depositing until it stops, where it lays down the rest. */
		void
		RunDroplet(
			const Grid&        grid,
			Channels&          channels,
			const Brush&       brush,
			const ErosionDesc& desc,
			const float        cellSize,
			glm::vec2          pos)
		{
			const auto limit = glm::vec2(
				static_cast<float>(grid.samplesX - 1),
				static_cast<float>(grid.samplesZ - 1));

			auto  dir      = glm::vec2(0.0f);
			float water    = 1.0f;
			float sediment = 0.0f;

			for (uint32_t step = 0; step < desc.maxSteps; ++step)
			{
				const auto              node  = glm::uvec2(pos);
				const size_t            index = grid.Index(node.x, node.y);
				const HeightAndGradient here  = Sample(grid, pos);
				const float             gLen  = glm::length(here.gradient);
				const float worn = 1.0f - std::exp(-channels.discharge[index] / c_ChannelWater);

				if (gLen > 1e-6f)
				{
					dir = dir * desc.inertia - here.gradient / gLen * (1.0f - desc.inertia);
				}
				const glm::vec2 flow    = channels.momentum[index];
				const float     flowLen = glm::length(flow);
				if (flowLen > 1e-6f)
				{
					dir += flow / flowLen * (desc.channelSteering * worn);
				}

				const float dirLen = glm::length(dir);
				if (dirLen < 1e-6f)
				{
					break;
				}
				dir /= dirLen;

				const glm::vec2 next = pos + dir;
				if (next.x < 0.0f || next.y < 0.0f || next.x >= limit.x || next.y >= limit.y)
				{
					break;
				}

				channels.dischargeTrack[index] += water;
				channels.momentumTrack[index] += dir * water;

				const float drop  = here.height - Sample(grid, next).height;
				const float carry = std::max(drop, desc.minSlope * cellSize) * water *
				                    desc.capacity * (1.0f + desc.channelErosion * worn);

				if (drop < 0.0f || sediment > carry)
				{
					const float laid = drop < 0.0f ? std::min(-drop, sediment) :
					                                 (sediment - carry) * desc.depositionRate;
					sediment -= laid;
					Spread(grid, pos, laid);
				}
				else
				{
					const float eroded = std::min((carry - sediment) * desc.erosionRate, drop);
					sediment += eroded;
					Wear(grid, brush, node, eroded);
				}

				water *= 1.0f - desc.evaporation;
				pos = next;
			}

			Spread(grid, pos, sediment);
		}

		/**
		 * One pass of droplets, `perSample` to a sample. The field is cut into tiles wider than two
		 * droplets' reach, and the tiles run in four phases of every other tile along each axis,
		 * so no two droplets running at once ever touch the same sample: each tile's droplets run
		 * in order from a key of the seed, the pass and the tile, and the result does not depend
		 * on which thread runs which tile.
		 */
		void
		HydraulicPass(
			const Grid&        grid,
			Channels&          channels,
			const Brush&       brush,
			const ErosionDesc& desc,
			const float        cellSize,
			const float        perSample,
			const uint32_t     seed,
			const uint32_t     pass)
		{
			// A droplet moves a cell a step, reads and writes the cell beyond where it stands, and
			// wears the brush's reach around it.
			const uint32_t tile   = 2 * (desc.maxSteps + 2 + brush.reach);
			const uint32_t tilesX = core::div_ceil(grid.samplesX - 1, tile);
			const uint32_t tilesZ = core::div_ceil(grid.samplesZ - 1, tile);

			for (uint32_t phase = 0; phase < 4; ++phase)
			{
				const uint32_t px     = phase & 1u;
				const uint32_t pz     = phase >> 1u;
				const uint32_t countX = (tilesX + 1 - px) / 2;
				const uint32_t countZ = (tilesZ + 1 - pz) / 2;

				core::parallel_for(
					static_cast<size_t>(countX) * countZ,
					0,
					"terrain erosion",
					[&](const size_t i) {
						const uint32_t tx = 2 * static_cast<uint32_t>(i % countX) + px;
						const uint32_t tz = 2 * static_cast<uint32_t>(i / countX) + pz;
						const auto     lo = glm::uvec2(tx * tile, tz * tile);
						const auto     hi = glm::min(
							lo + glm::uvec2(tile),
							glm::uvec2(grid.samplesX - 1, grid.samplesZ - 1));
						const auto size = glm::vec2(hi - lo);

						auto random = Random{ .key = core::hash_mix32(
												  seed ^ core::hash_mix32(
															 pass * 0x9e3779b9u ^
															 core::hash_mix32(tz * tilesX + tx))) };
						const auto droplets =
							static_cast<uint32_t>(std::lround(perSample * size.x * size.y));
						for (uint32_t d = 0; d < droplets; ++d)
						{
							const auto start =
								glm::vec2(lo) + glm::vec2(random.Next(), random.Next()) * size;
							RunDroplet(grid, channels, brush, desc, cellSize, start);
						}
					});
			}

			const float scale = 1.0f / perSample;
			for (size_t i = 0; i < channels.discharge.size(); ++i)
			{
				channels.discharge[i] = glm::mix(
					channels.discharge[i],
					channels.dischargeTrack[i] * scale,
					c_ChannelMemory);
				channels.momentum[i] = glm::mix(
					channels.momentum[i],
					channels.momentumTrack[i] * scale,
					c_ChannelMemory);
			}
			std::ranges::fill(channels.dischargeTrack, 0.0f);
			std::ranges::fill(channels.momentumTrack, glm::vec2(0.0f));
		}

		/**
		 * One thermal relaxation: each sample steeper than the talus angle above a neighbour sends
		 * a share of its steepest excess down to the neighbours it exceeds, split by how far each
		 * is exceeded. What one sample sends is taken from it whole and given to the others in
		 * parts that sum to it, all read from the heights before the iteration, so it conserves.
		 */
		void
		ThermalIteration(
			const Grid&         grid,
			std::vector<float>& scratch,
			std::vector<float>& outflow,
			std::vector<float>& excessSum,
			const float         talus,
			const float         rate,
			const float         cellSize)
		{
			const auto excessOf = [&](const uint32_t x, const uint32_t z, const glm::ivec2 n) {
				const int nx = static_cast<int>(x) + n.x;
				const int nz = static_cast<int>(z) + n.y;
				if (nx < 0 || nz < 0 || nx >= static_cast<int>(grid.samplesX) ||
				    nz >= static_cast<int>(grid.samplesZ))
				{
					return 0.0f;
				}
				const float distance = (n.x != 0 && n.y != 0) ?
				                           cellSize * static_cast<float>(core::c_Sqrt2) :
				                           cellSize;
				const float rise =
					grid.heights[grid.Index(x, z)] -
					grid.heights[grid.Index(static_cast<uint32_t>(nx), static_cast<uint32_t>(nz))];
				return std::max(rise - talus * distance, 0.0f);
			};

			core::parallel_for(grid.samplesZ, 0, "terrain thermal", [&](const size_t zi) {
				const auto z = static_cast<uint32_t>(zi);
				for (uint32_t x = 0; x < grid.samplesX; ++x)
				{
					float largest = 0.0f;
					float sum     = 0.0f;
					for (const glm::ivec2 n : core::c_Neighbours8)
					{
						const float excess = excessOf(x, z, n);
						largest            = std::max(largest, excess);
						sum += excess;
					}
					outflow[grid.Index(x, z)]   = largest * rate * 0.5f;
					excessSum[grid.Index(x, z)] = sum;
				}
			});

			core::parallel_for(grid.samplesZ, 0, "terrain thermal", [&](const size_t zi) {
				const auto z = static_cast<uint32_t>(zi);
				for (uint32_t x = 0; x < grid.samplesX; ++x)
				{
					float gained = 0.0f;
					for (const glm::ivec2 n : core::c_Neighbours8)
					{
						const int nx = static_cast<int>(x) + n.x;
						const int nz = static_cast<int>(z) + n.y;
						if (nx < 0 || nz < 0 || nx >= static_cast<int>(grid.samplesX) ||
						    nz >= static_cast<int>(grid.samplesZ))
						{
							continue;
						}
						const auto   ux    = static_cast<uint32_t>(nx);
						const auto   uz    = static_cast<uint32_t>(nz);
						const size_t from  = grid.Index(ux, uz);
						const float  share = excessOf(ux, uz, -n);
						if (share > 0.0f)
						{
							gained += outflow[from] * share / excessSum[from];
						}
					}
					const size_t i = grid.Index(x, z);
					scratch[i]     = grid.heights[i] - outflow[i] + gained;
				}
			});

			std::ranges::copy(scratch, grid.heights.begin());
		}
	}

	namespace
	{
		/**
		 * Cuts sample `at` down by `cut` metres from its `uncut` height, and the ground around it, out to `reach` cells, by
		 * a share of that tapering to nothing: on level ground a V whose sides rise `rise` a cell,
		 * banks that thermal erosion and the droplets widen rather than fill back in, and on a
		 * slope steeper than that the slope itself, lowered, rather than a facet cut through it.
		 */
		void
		Notch(
			const Grid&                  grid,
			const std::span<const float> uncut,
			const uint32_t               at,
			const float                  cut,
			const float                  rise) noexcept
		{
			const int   cx    = static_cast<int>(at % grid.samplesX);
			const int   cz    = static_cast<int>(at / grid.samplesX);
			const float cells = cut / rise;
			const int   reach = static_cast<int>(std::ceil(cells));
			for (int dz = -reach; dz <= reach; ++dz)
			{
				for (int dx = -reach; dx <= reach; ++dx)
				{
					const int x = cx + dx;
					const int z = cz + dz;
					if (x < 0 || z < 0 || x >= static_cast<int>(grid.samplesX) ||
					    z >= static_cast<int>(grid.samplesZ))
					{
						continue;
					}
					const float share = 1.0f - glm::length(glm::vec2(dx, dz)) / cells;
					if (share <= 0.0f)
					{
						continue;
					}
					float& h = grid.heights[grid.Index(
						static_cast<uint32_t>(x),
						static_cast<uint32_t>(z))];
					h        = std::min(
						h,
						uncut[grid.Index(static_cast<uint32_t>(x), static_cast<uint32_t>(z))] -
							cut * share);
				}
			}
		}
	}

	void
	BreachBasins(
		const std::span<float> heights,
		const uint32_t         samplesX,
		const uint32_t         samplesZ,
		const float            cellSize,
		const float            depth,
		const float            talus)
	{
		constexpr uint32_t c_None = ~0u;

		const auto grid = Grid{ .heights = heights, .samplesX = samplesX, .samplesZ = samplesZ };
		using Entry     = std::pair<float, uint32_t>;
		auto queue      = std::priority_queue<Entry, std::vector<Entry>, std::greater<>>();
		auto toward     = std::vector<uint32_t>(heights.size(), c_None);
		auto reached    = std::vector<bool>(heights.size(), false);
		for (uint32_t z = 0; z < samplesZ; ++z)
		{
			for (uint32_t x = 0; x < samplesX; ++x)
			{
				if (x == 0 || z == 0 || x == samplesX - 1 || z == samplesZ - 1)
				{
					const size_t i = grid.Index(x, z);
					reached[i]     = true;
					queue.emplace(heights[i], static_cast<uint32_t>(i));
				}
			}
		}

		const float fall = c_BasinSlope * cellSize;
		const float rise = std::min(talus, c_BreachBank) * cellSize;
		// A cut's banks reach as far as the ground it was cut from stood above it: measured on the
		// heights before any cut, since a cut lowers its neighbours along the way.
		const auto uncut = std::vector<float>(heights.begin(), heights.end());
		while (!queue.empty())
		{
			const uint32_t from = queue.top().second;
			queue.pop();
			const auto x = static_cast<int>(from % samplesX);
			const auto z = static_cast<int>(from / samplesX);
			for (const glm::ivec2 n : core::c_Neighbours8)
			{
				const int nx = x + n.x;
				const int nz = z + n.y;
				if (nx < 0 || nz < 0 || nx >= static_cast<int>(samplesX) ||
				    nz >= static_cast<int>(samplesZ))
				{
					continue;
				}
				const auto to = static_cast<uint32_t>(
					grid.Index(static_cast<uint32_t>(nx), static_cast<uint32_t>(nz)));
				if (reached[to])
				{
					continue;
				}
				reached[to] = true;
				toward[to]  = from;

				// Lower than the way out it was reached by: cut that way down below it, sample by
				// sample toward the edge until the ground is already low enough -- unless the cut
				// would go deeper than `depth` anywhere, when the hollow is left to hold water.
				if (heights[to] < heights[from])
				{
					bool  shallow = true;
					float level   = heights[to];
					for (uint32_t at = from; at != c_None; at = toward[at])
					{
						level -= fall;
						if (heights[at] <= level)
							break;
						if (heights[at] - level > depth)
						{
							shallow = false;
							break;
						}
					}
					if (shallow)
					{
						level = heights[to];
						for (uint32_t at = from; at != c_None; at = toward[at])
						{
							level -= fall;
							if (heights[at] <= level)
								break;
							Notch(grid, uncut, at, std::min(uncut[at] - level, depth), rise);
						}
					}
				}
				queue.emplace(heights[to], to);
			}
		}
	}

	void
	ValidateErosion(const ErosionDesc& desc)
	{
		const auto refuse = [](const char* what) {
			core::throw_runtime_error("terrain::Generate: erosion {} is out of range", what);
		};
		const auto finite = [](const float v) { return std::isfinite(v); };

		if (!finite(desc.dropletsPerSample) || desc.dropletsPerSample < 0.0f)
			refuse("dropletsPerSample");
		if (desc.dropletsPerSample > 0.0f && desc.passes == 0)
			refuse("passes");
		if (desc.dropletsPerSample > 0.0f && desc.maxSteps == 0)
			refuse("maxSteps");
		if (!finite(desc.inertia) || desc.inertia < 0.0f || desc.inertia >= 1.0f)
			refuse("inertia");
		if (!finite(desc.capacity) || desc.capacity < 0.0f)
			refuse("capacity");
		if (!finite(desc.minSlope) || desc.minSlope < 0.0f)
			refuse("minSlope");
		if (!finite(desc.breachDepth) || desc.breachDepth < 0.0f)
			refuse("breachDepth");
		if (!finite(desc.erosionRadius) || desc.erosionRadius <= 0.0f)
			refuse("erosionRadius");
		if (!finite(desc.erosionRate) || desc.erosionRate <= 0.0f || desc.erosionRate > 1.0f)
			refuse("erosionRate");
		if (!finite(desc.depositionRate) || desc.depositionRate <= 0.0f ||
		    desc.depositionRate > 1.0f)
			refuse("depositionRate");
		if (!finite(desc.evaporation) || desc.evaporation < 0.0f || desc.evaporation >= 1.0f)
			refuse("evaporation");
		if (!finite(desc.channelErosion) || desc.channelErosion < 0.0f)
			refuse("channelErosion");
		if (!finite(desc.channelSteering) || desc.channelSteering < 0.0f ||
		    desc.channelSteering > 1.0f)
			refuse("channelSteering");
		if (!finite(desc.talusDegrees) || desc.talusDegrees <= 0.0f || desc.talusDegrees >= 90.0f)
			refuse("talusDegrees");
		if (!finite(desc.thermalRate) || desc.thermalRate <= 0.0f || desc.thermalRate > 1.0f)
			refuse("thermalRate");
	}

	void
	Erode(
		const std::span<float> heights,
		const uint32_t         samplesX,
		const uint32_t         samplesZ,
		const float            cellSize,
		const ErosionDesc&     desc,
		const uint32_t         seed)
	{
		core::ensure(
			heights.size() == static_cast<size_t>(samplesX) * samplesZ && samplesX >= 2 &&
				samplesZ >= 2,
			"erosion over {} heights for {} x {} samples",
			heights.size(),
			samplesX,
			samplesZ);

		const auto grid  = Grid{ .heights = heights, .samplesX = samplesX, .samplesZ = samplesZ };
		const auto count = heights.size();

		auto channels = Channels{
			.discharge      = std::vector<float>(count, 0.0f),
			.momentum       = std::vector<glm::vec2>(count, glm::vec2(0.0f)),
			.dischargeTrack = std::vector<float>(count, 0.0f),
			.momentumTrack  = std::vector<glm::vec2>(count, glm::vec2(0.0f)),
		};
		auto scratch   = std::vector<float>(desc.thermalIterations > 0 ? count : 0);
		auto outflow   = std::vector<float>(scratch.size());
		auto excessSum = std::vector<float>(scratch.size());

		const uint32_t passes    = std::max(desc.passes, 1u);
		const float    perSample = desc.dropletsPerSample / static_cast<float>(passes);
		const float    talus     = std::tan(glm::radians(desc.talusDegrees));
		const Brush    brush     = MakeBrush(desc.erosionRadius, cellSize);
		const auto     breach    = [&] {
			if (desc.breachDepth > 0.0f)
			{
				BreachBasins(heights, samplesX, samplesZ, cellSize, desc.breachDepth, talus);
			}
		};
		// Before every pass and after the last: what a droplet lays down where it stops can dam a
		// channel again.
		for (uint32_t pass = 0; pass < passes; ++pass)
		{
			breach();
			if (perSample > 0.0f)
			{
				HydraulicPass(grid, channels, brush, desc, cellSize, perSample, seed, pass);
			}
			for (uint32_t i = 0; i < desc.thermalIterations; ++i)
			{
				ThermalIteration(
					grid,
					scratch,
					outflow,
					excessSum,
					talus,
					desc.thermalRate,
					cellSize);
			}
		}
		breach();
	}
}
