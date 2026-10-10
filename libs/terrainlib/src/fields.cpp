#include "grids.h"

#include <algorithm>
#include <array>
#include <assetlib_structs/Heightfield.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <core/math.h>
#include <core/parallel_for.h>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <terrainlib/fields.h>
#include <terrainlib/layer.h>
#include <utility>
#include <vector>

namespace terrain
{
	namespace
	{
		// The upstream areas wetness runs between, in square metres.
		constexpr float c_DryArea = 100.0f;
		constexpr float c_WetArea = 1.0e6f;

		// How sharply flow favours the steepest way down: 1 shares by slope, higher tends to one
		// line (Quinn et al. 1991; Holmgren 1994).
		constexpr float c_FlowExponent = 1.1f;

		[[nodiscard]] TerrainLayer
		LayerLike(const assetlib::Heightfield& field)
		{
			return TerrainLayer{ .samplesX = field.samplesX,
				                 .samplesZ = field.samplesZ,
				                 .cellSize = field.cellSize,
				                 .values   = std::vector<float>(field.heights.size(), 0.0f) };
		}
	}

	TerrainFields
	DeriveFields(const assetlib::Heightfield& field)
	{
		const uint32_t sx = field.samplesX;
		const uint32_t sz = field.samplesZ;
		core::ensure(
			sx >= 2 && sz >= 2 && field.heights.size() == static_cast<size_t>(sx) * sz,
			"fields of a heightfield of {} x {} samples holding {}",
			sx,
			sz,
			field.heights.size());

		const float cell  = field.cellSize;
		const float scale = field.heightRange / 65535.0f;
		const auto  index = [sx](const uint32_t x, const uint32_t z) {
			return static_cast<size_t>(z) * sx + x;
		};
		const auto height = [&](const int x, const int z) {
			const auto cx = static_cast<uint32_t>(std::clamp(x, 0, static_cast<int>(sx) - 1));
			const auto cz = static_cast<uint32_t>(std::clamp(z, 0, static_cast<int>(sz) - 1));
			return static_cast<float>(field.heights[index(cx, cz)]) * scale;
		};

		auto fields = TerrainFields{ .slope     = LayerLike(field),
			                         .curvature = LayerLike(field),
			                         .flow      = LayerLike(field),
			                         .wetness   = LayerLike(field),
			                         .lakeDepth = LayerLike(field) };

		// Central differences and the five-point Laplacian; past the edge a sample repeats the
		// edge's, so an edge reads as level across it.
		core::parallel_for(sz, 0, "terrain fields", [&](const size_t zi) {
			const auto z = static_cast<int>(zi);
			for (int x = 0; x < static_cast<int>(sx); ++x)
			{
				const float h  = height(x, z);
				const float l  = height(x - 1, z);
				const float r  = height(x + 1, z);
				const float d  = height(x, z - 1);
				const float u  = height(x, z + 1);
				const auto  dx = (x == 0 || x == static_cast<int>(sx) - 1) ? cell : 2.0f * cell;
				const auto  dz = (z == 0 || z == static_cast<int>(sz) - 1) ? cell : 2.0f * cell;

				const size_t i         = index(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
				fields.slope.values[i] = glm::length(glm::vec2((r - l) / dx, (u - d) / dz));
				fields.curvature.values[i] = (l + r + d + u - 4.0f * h) / (cell * cell);
			}
		});

		// Highest first, ties broken by position so the order is the same every run.
		auto order = std::vector<uint32_t>(field.heights.size());
		std::iota(order.begin(), order.end(), 0u);
		std::ranges::sort(order, [&](const uint32_t a, const uint32_t b) {
			return field.heights[a] != field.heights[b] ? field.heights[a] > field.heights[b] :
			                                              a < b;
		});

		std::vector<float>& flow = fields.flow.values;
		std::ranges::fill(flow, cell * cell);
		for (const uint32_t i : order)
		{
			const auto  x = static_cast<int>(i % sx);
			const auto  z = static_cast<int>(i / sx);
			const float h = static_cast<float>(field.heights[i]) * scale;

			auto  weights = std::array<float, 8>{};
			float total   = 0.0f;
			for (size_t n = 0; n < core::c_Neighbours8.size(); ++n)
			{
				const int nx = x + core::c_Neighbours8[n].x;
				const int nz = z + core::c_Neighbours8[n].y;
				if (nx < 0 || nz < 0 || nx >= static_cast<int>(sx) || nz >= static_cast<int>(sz))
				{
					continue;
				}
				const float drop = h - height(nx, nz);
				if (drop <= 0.0f)
				{
					continue;
				}
				const bool diagonal =
					core::c_Neighbours8[n].x != 0 && core::c_Neighbours8[n].y != 0;
				const float run = diagonal ? cell * static_cast<float>(core::c_Sqrt2) : cell;
				// The contour length the flow crosses toward a neighbour, in cells: Quinn's weighting.
				const float contour = diagonal ? 0.25f * static_cast<float>(core::c_Sqrt2) : 0.5f;
				weights[n]          = std::pow(drop / run, c_FlowExponent) * contour;
				total += weights[n];
			}
			if (total <= 0.0f)
			{
				continue;
			}
			for (size_t n = 0; n < core::c_Neighbours8.size(); ++n)
			{
				if (weights[n] > 0.0f)
				{
					const auto to = index(
						static_cast<uint32_t>(x + core::c_Neighbours8[n].x),
						static_cast<uint32_t>(z + core::c_Neighbours8[n].y));
					flow[to] += flow[i] * weights[n] / total;
				}
			}
		}

		const float logDry = std::log(c_DryArea);
		const float logWet = std::log(c_WetArea);
		for (size_t i = 0; i < flow.size(); ++i)
		{
			fields.wetness.values[i] =
				std::clamp((std::log(flow[i]) - logDry) / (logWet - logDry), 0.0f, 1.0f);
		}
		auto ground = std::vector<float>(field.heights.size());
		for (size_t i = 0; i < ground.size(); ++i)
			ground[i] = static_cast<float>(field.heights[i]) * scale;
		const std::vector<float> filled = FloodFromEdge(ground, sx, sz).filled;
		for (size_t i = 0; i < filled.size(); ++i)
		{
			fields.lakeDepth.values[i] = filled[i] - static_cast<float>(field.heights[i]) * scale;
		}
		return fields;
	}
}
