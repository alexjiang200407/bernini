#include "grids.h"

#include <algorithm>
#include <assetlib_structs/Heightfield.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <core/hash.h>
#include <core/math.h>
#include <core/noise.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <queue>
#include <terrainlib/types/TerrainLayer.h>
#include <terrainlib/types/TerrainWater.h>
#include <terrainlib/types/WaterDesc.h>
#include <terrainlib/water.h>
#include <utility>
#include <vector>

// docs/terrain.md § Water says what each step means.

namespace terrain
{
	namespace
	{
		constexpr uint32_t c_None     = ~0u;
		constexpr uint32_t c_LakeSeed = 0x5bd1e995u;

		// Metres of water below which a sample counts as dry: a shore's last film is not a lake.
		constexpr float c_Film = 1e-3f;

		// Square metres a river's width is measured against: `RiverRule::width` is its width here.
		constexpr float c_WidthArea = 1.0e6f;

		struct Grid
		{
			uint32_t samplesX;
			uint32_t samplesZ;
			float    cellSize;

			[[nodiscard]] size_t
			Count() const noexcept
			{
				return static_cast<size_t>(samplesX) * samplesZ;
			}

			[[nodiscard]] size_t
			Index(const uint32_t x, const uint32_t z) const noexcept
			{
				return static_cast<size_t>(z) * samplesX + x;
			}

			[[nodiscard]] glm::vec2
			Position(const size_t i) const noexcept
			{
				return glm::vec2(
					static_cast<float>(i % samplesX) * cellSize,
					static_cast<float>(i / samplesX) * cellSize);
			}

			/** Every sample whose position lies within `radius` metres of `centre`, by index. */
			void
			ForEachNear(
				const glm::vec2                           centre,
				const float                               radius,
				const std::function<void(size_t, float)>& visit) const
			{
				const glm::vec2 lo = (centre - radius) / cellSize;
				const glm::vec2 hi = (centre + radius) / cellSize;
				const int       x0 = std::max(static_cast<int>(std::floor(lo.x)), 0);
				const int       z0 = std::max(static_cast<int>(std::floor(lo.y)), 0);
				const int       x1 =
					std::min(static_cast<int>(std::ceil(hi.x)), static_cast<int>(samplesX) - 1);
				const int z1 =
					std::min(static_cast<int>(std::ceil(hi.y)), static_cast<int>(samplesZ) - 1);
				for (int z = z0; z <= z1; ++z)
				{
					for (int x = x0; x <= x1; ++x)
					{
						const size_t i = Index(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
						const float  distance = glm::distance(Position(i), centre);
						if (distance <= radius)
							visit(i, distance);
					}
				}
			}
		};

		void
		Validate(const WaterDesc& desc)
		{
			const auto positive = [](const float v) { return std::isfinite(v) && v > 0.0f; };
			const auto refuse   = [](const char* what) {
				core::throw_runtime_error("terrain::CarveWater: {} is out of range", what);
			};

			const RiverRule& rivers = desc.rivers;
			if (!std::isfinite(rivers.area) || rivers.area < 0.0f)
				refuse("rivers.area");
			if (rivers.area > 0.0f)
			{
				if (!positive(rivers.width))
					refuse("rivers.width");
				if (!positive(rivers.minWidth) || !positive(rivers.maxWidth) ||
				    rivers.minWidth > rivers.maxWidth)
					refuse("rivers.minWidth");
				if (!positive(rivers.depth))
					refuse("rivers.depth");
				if (!positive(rivers.minDepth) || !positive(rivers.maxDepth) ||
				    rivers.minDepth > rivers.maxDepth)
					refuse("rivers.minDepth");
				if (!positive(rivers.bank))
					refuse("rivers.bank");
				if (!std::isfinite(rivers.smoothing) || rivers.smoothing < 0.0f)
					refuse("rivers.smoothing");
				if (!std::isfinite(rivers.minLength) || rivers.minLength < 0.0f)
					refuse("rivers.minLength");
				if (!positive(rivers.speed) || !positive(rivers.maxSpeed) ||
				    rivers.speed > rivers.maxSpeed)
					refuse("rivers.speed");
				if (!std::isfinite(rivers.speedSlope) || rivers.speedSlope < 0.0f)
					refuse("rivers.speedSlope");
			}

			const LakeRule& lakes = desc.lakes;
			if (lakes.count > 0)
			{
				if (!positive(lakes.minRadius) || !positive(lakes.maxRadius) ||
				    lakes.minRadius > lakes.maxRadius)
					refuse("lakes.minRadius");
				if (!positive(lakes.depth))
					refuse("lakes.depth");
				if (!positive(lakes.maxSlope))
					refuse("lakes.maxSlope");
				if (!std::isfinite(lakes.spacing) || lakes.spacing < 0.0f)
					refuse("lakes.spacing");
				if (!positive(lakes.bank))
					refuse("lakes.bank");
				if (!std::isfinite(lakes.shoreNoise) || lakes.shoreNoise < 0.0f ||
				    lakes.shoreNoise >= 1.0f)
					refuse("lakes.shoreNoise");
			}
		}

		/** Where each sample drains to, and how much ground drains through it. */
		struct Drainage
		{
			std::vector<uint32_t> receiver;  // c_None on the field's edge, where water leaves
			std::vector<float>    area;      // square metres, the sample's own cell included
		};

		/**
		 * A priority flood from the field's edge (Barnes, Lehman and Mulla 2014): a sample drains to
		 * the neighbour the flood reached it from, its lowest by the ground as filled, so a hollow
		 * drains over its rim and every sample reaches the edge.
		 */
		[[nodiscard]] Drainage
		Drain(const Grid& grid, const std::vector<float>& heights)
		{
			using Entry = std::pair<float, uint32_t>;
			auto queue  = std::priority_queue<Entry, std::vector<Entry>, std::greater<>>();
			auto drained =
				Drainage{ .receiver = std::vector<uint32_t>(grid.Count(), c_None),
				          .area = std::vector<float>(grid.Count(), grid.cellSize * grid.cellSize) };
			auto reached = std::vector<bool>(grid.Count(), false);
			auto order   = std::vector<uint32_t>();
			order.reserve(grid.Count());
			for (uint32_t z = 0; z < grid.samplesZ; ++z)
			{
				for (uint32_t x = 0; x < grid.samplesX; ++x)
				{
					if (x == 0 || z == 0 || x == grid.samplesX - 1 || z == grid.samplesZ - 1)
					{
						const size_t i = grid.Index(x, z);
						reached[i]     = true;
						queue.emplace(heights[i], static_cast<uint32_t>(i));
					}
				}
			}
			while (!queue.empty())
			{
				const auto [level, from] = queue.top();
				queue.pop();
				order.push_back(from);
				const auto x = static_cast<int>(from % grid.samplesX);
				const auto z = static_cast<int>(from / grid.samplesX);
				for (const glm::ivec2 n : core::c_Neighbours8)
				{
					const int nx = x + n.x;
					const int nz = z + n.y;
					if (nx < 0 || nz < 0 || nx >= static_cast<int>(grid.samplesX) ||
					    nz >= static_cast<int>(grid.samplesZ))
						continue;
					const size_t to =
						grid.Index(static_cast<uint32_t>(nx), static_cast<uint32_t>(nz));
					if (reached[to])
						continue;
					reached[to]          = true;
					drained.receiver[to] = from;
					queue.emplace(std::max(level, heights[to]), static_cast<uint32_t>(to));
				}
			}
			for (auto it = order.rbegin(); it != order.rend(); ++it)
			{
				if (drained.receiver[*it] != c_None)
					drained.area[drained.receiver[*it]] += drained.area[*it];
			}
			return drained;
		}

		/** A river being laid out: its samples from head to mouth, and the river it joins. */
		struct Course
		{
			std::vector<uint32_t>  samples;
			std::vector<glm::vec2> points;  // smoothed, one per sample, and the junction last
			std::vector<float>     area;
			uint32_t               joins      = c_None;  // the river this one flows into
			uint32_t               joinsPoint = 0;       // the point of that river it ends on
		};

		[[nodiscard]] float
		Length(const std::vector<glm::vec2>& points) noexcept
		{
			float length = 0.0f;
			for (size_t i = 1; i < points.size(); ++i)
				length += glm::distance(points[i - 1], points[i]);
			return length;
		}

		/**
		 * A moving average over `reach` points either side, narrowed toward each end so the head
		 * and the mouth stay where they are.
		 */
		[[nodiscard]] std::vector<glm::vec2>
		Smooth(const std::vector<glm::vec2>& points, const size_t reach)
		{
			auto smooth = points;
			for (size_t i = 1; i + 1 < points.size(); ++i)
			{
				const size_t k   = std::min({ reach, i, points.size() - 1 - i });
				auto         sum = glm::vec2(0.0f);
				for (size_t j = i - k; j <= i + k; ++j) sum += points[j];
				smooth[i] = sum / static_cast<float>(2 * k + 1);
			}
			return smooth;
		}

		/**
		 * The rivers' courses, the larger branch of each confluence first: a stem is walked upstream
		 * from a river sample on the field's edge through its largest donor, and each other donor
		 * starts a tributary that ends on the stem. A course shorter than the rule's minimum is
		 * dropped, and every tributary of it.
		 */
		[[nodiscard]] std::vector<Course>
		TraceRivers(const Grid& grid, const Drainage& drained, const RiverRule& rule)
		{
			const size_t count   = grid.Count();
			const auto   isRiver = [&](const uint32_t i) { return drained.area[i] >= rule.area; };

			// Each river sample's river donors, in a compressed table.
			auto first = std::vector<uint32_t>(count + 1, 0);
			for (uint32_t i = 0; i < count; ++i)
			{
				if (isRiver(i) && drained.receiver[i] != c_None)
					++first[drained.receiver[i] + 1];
			}
			for (size_t i = 0; i < count; ++i) first[i + 1] += first[i];
			auto donors = std::vector<uint32_t>(first[count]);
			auto filled = std::vector<uint32_t>(first.begin(), first.end() - 1);
			for (uint32_t i = 0; i < count; ++i)
			{
				if (isRiver(i) && drained.receiver[i] != c_None)
					donors[filled[drained.receiver[i]]++] = i;
			}

			struct Stem
			{
				uint32_t start;
				uint32_t joins;
				uint32_t joinsPoint;
			};
			auto stems   = std::deque<Stem>();
			auto outlets = std::vector<uint32_t>();
			for (uint32_t i = 0; i < count; ++i)
			{
				if (isRiver(i) && drained.receiver[i] == c_None)
					outlets.push_back(i);
			}
			std::ranges::sort(outlets, [&](const uint32_t a, const uint32_t b) {
				return drained.area[a] != drained.area[b] ? drained.area[a] > drained.area[b] :
				                                            a < b;
			});
			for (const uint32_t outlet : outlets) stems.push_back({ outlet, c_None, 0 });

			const auto reach =
				static_cast<size_t>(std::lround(rule.smoothing / (2.0f * grid.cellSize)));
			auto rivers   = std::vector<Course>();
			auto branches = std::vector<std::pair<uint32_t, size_t>>();
			while (!stems.empty())
			{
				const Stem stem = stems.front();
				stems.pop_front();

				auto upstream = std::vector<uint32_t>{ stem.start };
				branches.clear();
				for (uint32_t at = stem.start;;)
				{
					uint32_t best = c_None;
					for (uint32_t d = first[at]; d < first[at + 1]; ++d)
					{
						const uint32_t donor = donors[d];
						if (best == c_None || drained.area[donor] > drained.area[best] ||
						    (drained.area[donor] == drained.area[best] && donor < best))
							best = donor;
					}
					for (uint32_t d = first[at]; d < first[at + 1]; ++d)
					{
						if (donors[d] != best)
							branches.emplace_back(donors[d], upstream.size() - 1);
					}
					if (best == c_None)
						break;
					upstream.push_back(best);
					at = best;
				}

				auto course    = Course{ .joins = stem.joins, .joinsPoint = stem.joinsPoint };
				course.samples = std::vector<uint32_t>(upstream.rbegin(), upstream.rend());
				for (const uint32_t s : course.samples)
				{
					course.points.push_back(grid.Position(s));
					course.area.push_back(drained.area[s]);
				}
				if (stem.joins != c_None)
				{
					course.points.push_back(rivers[stem.joins].points[stem.joinsPoint]);
					course.area.push_back(drained.area[course.samples.back()]);
				}
				if (Length(course.points) < rule.minLength || course.points.size() < 2)
					continue;
				course.points = Smooth(course.points, reach);

				const auto index = static_cast<uint32_t>(rivers.size());
				const auto last  = upstream.size() - 1;
				for (const auto& [donor, at] : branches)
					stems.push_back({ donor, index, static_cast<uint32_t>(last - at) });
				rivers.push_back(std::move(course));
			}
			return rivers;
		}

		/** A lake as it is dug: its middle and its shore's radius in every direction. */
		struct Basin
		{
			glm::vec2 centre;
			float     radius;
			float     level;
			uint32_t  seed;
			float     noise;

			[[nodiscard]] float
			Shore(const glm::vec2 direction) const noexcept
			{
				return radius * (1.0f + noise * core::gradient_noise(direction * 1.7f, seed));
			}

			[[nodiscard]] float
			Reach() const noexcept
			{
				return radius * (1.0f + noise);
			}
		};

		[[nodiscard]] float
		Unit(const uint32_t seed, const uint32_t n) noexcept
		{
			return static_cast<float>(core::hash_mix32(seed ^ core::hash_mix32(n)) >> 8) *
			       (1.0f / 16777216.0f);
		}

		/**
		 * The lakes' sites, best first: flat ground, low against the ground around it and gathering
		 * water, far enough from the field's edge for the whole lake and its banks, and `spacing`
		 * apart.
		 */
		[[nodiscard]] std::vector<Basin>
		SiteLakes(
			const Grid&               grid,
			const std::vector<float>& heights,
			const Drainage&           drained,
			const LakeRule&           rule,
			const uint32_t            seed)
		{
			auto slope = std::vector<float>(grid.Count());
			for (uint32_t z = 0; z < grid.samplesZ; ++z)
			{
				for (uint32_t x = 0; x < grid.samplesX; ++x)
				{
					const uint32_t l  = x > 0 ? x - 1 : x;
					const uint32_t r  = x + 1 < grid.samplesX ? x + 1 : x;
					const uint32_t d  = z > 0 ? z - 1 : z;
					const uint32_t u  = z + 1 < grid.samplesZ ? z + 1 : z;
					const float    dx = (heights[grid.Index(r, z)] - heights[grid.Index(l, z)]) /
					                    (static_cast<float>(r - l) * grid.cellSize);
					const float    dz = (heights[grid.Index(x, u)] - heights[grid.Index(x, d)]) /
					                    (static_cast<float>(u - d) * grid.cellSize);
					slope[grid.Index(x, z)] = glm::length(glm::vec2(dx, dz));
				}
			}
			const int lakeCells =
				std::max(static_cast<int>(std::lround(rule.maxRadius / grid.cellSize)), 1);
			const auto flatness = BoxMean(slope, grid.samplesX, grid.samplesZ, lakeCells);
			const auto around   = BoxMean(heights, grid.samplesX, grid.samplesZ, 3 * lakeCells);

			struct Candidate
			{
				float    score;
				uint32_t index;
			};
			auto       candidates = std::vector<Candidate>();
			const auto margin =
				static_cast<uint32_t>(std::ceil(
					(rule.maxRadius * (1.0f + rule.shoreNoise) + rule.bank) / grid.cellSize)) +
				1;
			const auto step =
				static_cast<uint32_t>(std::max(rule.minRadius / (2.0f * grid.cellSize), 1.0f));
			for (uint32_t z = margin; z + margin < grid.samplesZ; z += step)
			{
				for (uint32_t x = margin; x + margin < grid.samplesX; x += step)
				{
					const size_t i = grid.Index(x, z);
					if (flatness[i] > rule.maxSlope)
						continue;
					// Low ground by tens of metres, wet ground by decades of area, flat by the rule's
					// limit, and a little chance so a field of equals does not pick in raster order.
					const float low  = (around[i] - heights[i]) / 10.0f;
					const float wet  = std::log10(drained.area[i]);
					const float flat = 1.0f - flatness[i] / rule.maxSlope;
					candidates.push_back(
						{ low + wet + flat + 0.5f * Unit(seed, static_cast<uint32_t>(i)),
					      static_cast<uint32_t>(i) });
				}
			}
			std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) {
				return a.score != b.score ? a.score > b.score : a.index < b.index;
			});

			auto basins = std::vector<Basin>();
			for (const Candidate& c : candidates)
			{
				if (basins.size() >= rule.count)
					break;
				const glm::vec2 centre = grid.Position(c.index);
				const bool      spaced = std::ranges::none_of(basins, [&](const Basin& b) {
					return glm::distance(b.centre, centre) < rule.spacing;
				});
				if (!spaced)
					continue;
				const auto n = static_cast<uint32_t>(basins.size());
				basins.push_back(
					Basin{ .centre = centre,
				           .radius =
				               glm::mix(rule.minRadius, rule.maxRadius, Unit(seed ^ c_LakeSeed, n)),
				           .level = 0.0f,
				           .seed  = core::hash_mix32(seed ^ c_LakeSeed ^ (n + 1)),
				           .noise = rule.shoreNoise });
			}

			// A lake stands at the lowest point of its rim: where it would spill.
			for (Basin& basin : basins)
			{
				basin.level = std::numeric_limits<float>::max();
				const auto steps =
					static_cast<int>(std::ceil(core::c_Pi * 2.0 * basin.Reach() / grid.cellSize));
				for (int s = 0; s < steps; ++s)
				{
					const float angle = static_cast<float>(s) * 2.0f *
					                    static_cast<float>(core::c_Pi) / static_cast<float>(steps);
					const glm::vec2 direction = glm::vec2(std::cos(angle), std::sin(angle));
					const glm::vec2 at =
						(basin.centre + direction * basin.Shore(direction)) / grid.cellSize;
					const auto x = static_cast<uint32_t>(
						std::clamp(std::lround(at.x), 0l, static_cast<long>(grid.samplesX) - 1));
					const auto z = static_cast<uint32_t>(
						std::clamp(std::lround(at.y), 0l, static_cast<long>(grid.samplesZ) - 1));
					basin.level = std::min(basin.level, heights[grid.Index(x, z)]);
				}
			}
			return basins;
		}

		[[nodiscard]] bool
		InLake(const Basin& basin, const glm::vec2 at) noexcept
		{
			const glm::vec2 offset   = at - basin.centre;
			const float     distance = glm::length(offset);
			return distance <=
			       basin.Shore(distance > 0.0f ? offset / distance : glm::vec2(1.0f, 0.0f));
		}

		/** What the water at a sample is, as the river or lake there that stands highest says. */
		struct Standing
		{
			std::vector<float>     surface;
			std::vector<glm::vec2> flow;
		};

		void
		Stand(Standing& water, const size_t i, const float surface, const glm::vec2 flow) noexcept
		{
			if (surface > water.surface[i])
			{
				water.surface[i] = surface;
				water.flow[i]    = flow;
			}
		}

		[[nodiscard]] TerrainLayer
		LayerOf(const Grid& grid, std::vector<float> values)
		{
			return TerrainLayer{ .samplesX = grid.samplesX,
				                 .samplesZ = grid.samplesZ,
				                 .cellSize = grid.cellSize,
				                 .values   = std::move(values) };
		}
	}

	TerrainWater
	CarveWater(assetlib::Heightfield& field, const WaterDesc& desc)
	{
		Validate(desc);
		const auto grid = Grid{ field.samplesX, field.samplesZ, field.cellSize };
		core::ensure(
			grid.samplesX >= 2 && grid.samplesZ >= 2 && field.heights.size() == grid.Count(),
			"water carved into a heightfield of {} x {} samples holding {}",
			grid.samplesX,
			grid.samplesZ,
			field.heights.size());

		const float scale  = field.heightRange / 65535.0f;
		auto        ground = std::vector<float>(grid.Count());
		for (size_t i = 0; i < grid.Count(); ++i)
			ground[i] = field.minHeight + static_cast<float>(field.heights[i]) * scale;
		const std::vector<float> uncut = ground;

		const Drainage   drained = Drain(grid, uncut);
		const RiverRule& rule    = desc.rivers;
		auto courses = rule.area > 0.0f ? TraceRivers(grid, drained, rule) : std::vector<Course>();
		auto basins  = desc.lakes.count > 0 ?
		                   SiteLakes(grid, uncut, drained, desc.lakes, desc.seed) :
		                   std::vector<Basin>();

		auto water = TerrainWater();
		water.rivers.resize(courses.size());
		const auto widthAt = [&](const float area) {
			return std::clamp(
				rule.width * std::sqrt(area / c_WidthArea),
				rule.minWidth,
				rule.maxWidth);
		};

		// Each river's surface: no higher than the ground across its width, at its lake's level
		// inside one, never rising downstream, and on a tributary meeting the river it joins. A
		// lake stands no higher than the lowest river through it.
		auto floors = std::vector<std::vector<float>>(courses.size());
		for (size_t r = 0; r < courses.size(); ++r)
		{
			const Course& course = courses[r];
			WaterRiver&   river  = water.rivers[r];
			river.course         = course.points;
			river.width.resize(course.points.size());
			floors[r].resize(course.points.size());
			for (size_t p = 0; p < course.points.size(); ++p)
			{
				river.width[p] = widthAt(course.area[p]);
				float low      = std::numeric_limits<float>::max();
				grid.ForEachNear(
					course.points[p],
					std::max(0.5f * river.width[p], grid.cellSize),
					[&](const size_t i, float) { low = std::min(low, uncut[i]); });
				floors[r][p] = low;
				for (Basin& basin : basins)
				{
					if (InLake(basin, course.points[p]))
						basin.level = std::min(basin.level, low);
				}
			}
		}
		const auto smoothing =
			static_cast<size_t>(std::lround(rule.smoothing / (2.0f * grid.cellSize)));
		for (size_t r = 0; r < courses.size(); ++r)
		{
			WaterRiver&         river = water.rivers[r];
			std::vector<float>& low   = floors[r];
			const size_t        n     = low.size();
			for (size_t p = 0; p < n; ++p)
			{
				for (const Basin& basin : basins)
				{
					if (InLake(basin, river.course[p]))
						low[p] = basin.level;
				}
				if (p > 0)
					low[p] = std::min(low[p], low[p - 1]);
			}

			// Smoothed into slopes, but never above the ground: what averaging would lift past the
			// lowest it may stand at is held there.
			river.surface = low;
			for (size_t p = 1; p + 1 < n; ++p)
			{
				const size_t k   = std::min({ smoothing, p, n - 1 - p });
				float        sum = 0.0f;
				for (size_t j = p - k; j <= p + k; ++j) sum += low[j];
				river.surface[p] = std::min(sum / static_cast<float>(2 * k + 1), low[p]);
			}
			for (size_t p = 1; p < n; ++p)
				river.surface[p] = std::min(river.surface[p], river.surface[p - 1]);
			const Course& course = courses[r];
			if (course.joins != c_None)
			{
				const float meets = water.rivers[course.joins].surface[course.joinsPoint];
				for (float& s : river.surface) s = std::max(s, meets);
				river.surface.back() = meets;
			}
		}
		for (const Basin& basin : basins)
		{
			water.lakes.push_back(
				WaterLake{ .centre = basin.centre, .radius = basin.radius, .level = basin.level });
		}

		auto standing =
			Standing{ .surface =
			              std::vector<float>(grid.Count(), std::numeric_limits<float>::lowest()),
			          .flow = std::vector<glm::vec2>(grid.Count(), glm::vec2(0.0f)) };

		// Each channel: a bed `depth` below the surface at its middle rising to it at the banks,
		// then banks rising back to the ground beside them.
		for (const WaterRiver& river : water.rivers)
		{
			for (size_t p = 0; p + 1 < river.course.size(); ++p)
			{
				const glm::vec2 a      = river.course[p];
				const glm::vec2 b      = river.course[p + 1];
				const glm::vec2 along  = b - a;
				const float     length = glm::length(along);
				const float     fall =
					length > 0.0f ? (river.surface[p] - river.surface[p + 1]) / length : 0.0f;
				const glm::vec2 flow =
					length > 0.0f ?
						along / length *
							std::min(rule.speed + rule.speedSlope * fall, rule.maxSpeed) :
						glm::vec2(0.0f);
				const float reach = 0.5f * std::max(river.width[p], river.width[p + 1]) + rule.bank;
				grid.ForEachNear(0.5f * (a + b), 0.5f * length + reach, [&](const size_t i, float) {
					const glm::vec2 at = grid.Position(i);
					const float     t =
						length > 0.0f ?
							std::clamp(glm::dot(at - a, along) / (length * length), 0.0f, 1.0f) :
							0.0f;
					const float distance = glm::distance(at, a + along * t);
					const float half     = 0.5f * glm::mix(river.width[p], river.width[p + 1], t);
					const float surface  = glm::mix(river.surface[p], river.surface[p + 1], t);
					if (distance <= half)
					{
						const float deep =
							std::clamp(rule.depth * 2.0f * half, rule.minDepth, rule.maxDepth);
						const float share = distance / half;
						ground[i] = std::min(ground[i], surface - deep * (1.0f - share * share));
						Stand(standing, i, surface, flow);
					}
					else if (distance < half + rule.bank)
					{
						const float rise = glm::smoothstep(half, half + rule.bank, distance);
						ground[i]        = std::min(ground[i], glm::mix(surface, uncut[i], rise));
					}
				});
			}
		}

		// Each lake: a bowl `depth` below its level at its middle, rising to its level at the shore,
		// then banks rising back to the ground.
		const LakeRule& lakes = desc.lakes;
		for (const Basin& basin : basins)
		{
			grid.ForEachNear(
				basin.centre,
				basin.Reach() + lakes.bank,
				[&](const size_t i, const float distance) {
					const glm::vec2 offset = grid.Position(i) - basin.centre;
					const float     shore =
						basin.Shore(distance > 0.0f ? offset / distance : glm::vec2(1.0f, 0.0f));
					if (distance <= shore)
					{
						const float share = distance / shore;
						ground[i] =
							std::min(ground[i], basin.level - lakes.depth * (1.0f - share * share));
						standing.surface[i] = std::max(standing.surface[i], basin.level);
						standing.flow[i]    = glm::vec2(0.0f);
					}
					else if (distance < shore + lakes.bank)
					{
						const float rise = glm::smoothstep(shore, shore + lakes.bank, distance);
						ground[i] = std::min(ground[i], glm::mix(basin.level, uncut[i], rise));
					}
				});
		}

		auto surface = std::vector<float>(grid.Count());
		auto depth   = std::vector<float>(grid.Count(), 0.0f);
		auto flowX   = std::vector<float>(grid.Count(), 0.0f);
		auto flowZ   = std::vector<float>(grid.Count(), 0.0f);
		auto wet     = std::vector<float>(grid.Count(), 0.0f);
		auto dry     = std::vector<float>(grid.Count(), 1.0f);
		for (size_t i = 0; i < grid.Count(); ++i)
		{
			if (standing.surface[i] > ground[i] + c_Film)
			{
				surface[i] = standing.surface[i];
				depth[i]   = standing.surface[i] - ground[i];
				flowX[i]   = standing.flow[i].x;
				flowZ[i]   = standing.flow[i].y;
				wet[i]     = 1.0f;
				dry[i]     = 0.0f;
			}
			else
			{
				surface[i] = ground[i];
			}
		}
		const std::vector<float> landward =
			DistanceInside(dry, grid.samplesX, grid.samplesZ, grid.cellSize);
		const std::vector<float> seaward =
			DistanceInside(wet, grid.samplesX, grid.samplesZ, grid.cellSize);
		auto shore = std::vector<float>(grid.Count());
		for (size_t i = 0; i < grid.Count(); ++i) shore[i] = landward[i] - seaward[i];

		water.surface = LayerOf(grid, std::move(surface));
		water.depth   = LayerOf(grid, std::move(depth));
		water.flowX   = LayerOf(grid, std::move(flowX));
		water.flowZ   = LayerOf(grid, std::move(flowZ));
		water.shore   = LayerOf(grid, std::move(shore));

		const auto [lowest, highest] = std::ranges::minmax(ground);
		const float range            = std::max(highest - lowest, 1e-3f);
		field.minHeight              = lowest;
		field.heightRange            = range;
		for (size_t i = 0; i < grid.Count(); ++i)
		{
			const float share = glm::clamp((ground[i] - lowest) / range, 0.0f, 1.0f);
			field.heights[i]  = static_cast<uint16_t>(std::lround(share * 65535.0f));
		}
		return water;
	}
}
