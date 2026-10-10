#include "grids.h"

#include <algorithm>
#include <cmath>
#include <core/glm.h>
#include <core/math.h>
#include <core/parallel_for.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace terrain
{
	namespace
	{
		/** The squared distance transform of one row: for each i, the least (i - j)^2 + f[j] over j, in place. */
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
	}

	std::vector<float>
	DistanceInside(
		const std::vector<float>& mask,
		const uint32_t            samplesX,
		const uint32_t            samplesZ,
		const float               cellSize)
	{
		// Far enough to stand for no sample outside at all, small enough to square in a float.
		constexpr float c_Far = 1.0e12f;

		const uint32_t sx   = samplesX;
		const uint32_t sz   = samplesZ;
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
		for (float& g : grid) g = std::sqrt(g) * cellSize;
		return grid;
	}

	std::vector<float>
	BoxMean(
		const std::vector<float>& values,
		const uint32_t            samplesX,
		const uint32_t            samplesZ,
		const int                 radius)
	{
		const uint32_t sx   = samplesX;
		const uint32_t sz   = samplesZ;
		const int      r    = radius;
		auto           sums = std::vector<double>(static_cast<size_t>(sx + 1) * (sz + 1), 0.0);
		const auto     at   = [sx](const uint32_t x, const uint32_t z) {
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
		core::parallel_for(sz, 0, "terrain box mean", [&](const size_t zi) {
			const auto z  = static_cast<int>(zi);
			const auto z0 = static_cast<uint32_t>(std::max(z - r, 0));
			const auto z1 = static_cast<uint32_t>(std::min(z + r + 1, static_cast<int>(sz)));
			for (int x = 0; x < static_cast<int>(sx); ++x)
			{
				const auto   x0 = static_cast<uint32_t>(std::max(x - r, 0));
				const auto   x1 = static_cast<uint32_t>(std::min(x + r + 1, static_cast<int>(sx)));
				const double sum =
					sums[at(x1, z1)] - sums[at(x0, z1)] - sums[at(x1, z0)] + sums[at(x0, z0)];
				mean[static_cast<size_t>(z) * sx + static_cast<uint32_t>(x)] =
					static_cast<float>(sum / static_cast<double>((x1 - x0) * (z1 - z0)));
			}
		});
		return mean;
	}

	Flood
	FloodFromEdge(
		const std::vector<float>& heights,
		const uint32_t            samplesX,
		const uint32_t            samplesZ)
	{
		const uint32_t sx    = samplesX;
		const uint32_t sz    = samplesZ;
		const size_t   count = heights.size();
		using Entry          = std::pair<float, uint32_t>;
		auto queue           = std::priority_queue<Entry, std::vector<Entry>, std::greater<>>();
		auto flood           = Flood{ .filled   = std::vector<float>(count, 0.0f),
			                          .receiver = std::vector<uint32_t>(count, c_NoSample),
			                          .order    = std::vector<uint32_t>() };
		flood.order.reserve(count);
		auto reached = std::vector<bool>(count, false);
		for (uint32_t z = 0; z < sz; ++z)
		{
			for (uint32_t x = 0; x < sx; ++x)
			{
				if (x == 0 || z == 0 || x == sx - 1 || z == sz - 1)
				{
					const size_t i  = static_cast<size_t>(z) * sx + x;
					reached[i]      = true;
					flood.filled[i] = heights[i];
					queue.emplace(heights[i], static_cast<uint32_t>(i));
				}
			}
		}
		while (!queue.empty())
		{
			const auto [level, from] = queue.top();
			queue.pop();
			flood.order.push_back(from);
			const auto x = static_cast<int>(from % sx);
			const auto z = static_cast<int>(from / sx);
			for (const glm::ivec2 n : core::c_Neighbours8)
			{
				const int nx = x + n.x;
				const int nz = z + n.y;
				if (nx < 0 || nz < 0 || nx >= static_cast<int>(sx) || nz >= static_cast<int>(sz))
					continue;
				const size_t to = static_cast<size_t>(nz) * sx + static_cast<size_t>(nx);
				if (reached[to])
					continue;
				reached[to]        = true;
				flood.receiver[to] = from;
				flood.filled[to]   = std::max(level, heights[to]);
				queue.emplace(flood.filled[to], static_cast<uint32_t>(to));
			}
		}
		return flood;
	}
}
