#pragma once
#include <algorithm>
#include <core/glm.h>
#include <core/type_traits.h>
#include <cstdint>

namespace terrain
{
	/**
	 * A grid of `samplesX` by `samplesZ` samples `cellSize` apart read at `local`, its offset from
	 * sample (0, 0): bilinear between the four samples `at(x, z)` returns, clamped at the edge.
	 */
	template <core::type_traits::invocable_returning<float, uint32_t, uint32_t> At>
	[[nodiscard]] float
	Bilinear(
		const uint32_t  samplesX,
		const uint32_t  samplesZ,
		const float     cellSize,
		const glm::vec2 local,
		const At&       at) noexcept
	{
		const auto maxCell =
			glm::vec2(static_cast<float>(samplesX - 1), static_cast<float>(samplesZ - 1));
		const auto cell = glm::clamp(local / cellSize, glm::vec2(0.0f), maxCell);

		const auto x0 = static_cast<uint32_t>(cell.x);
		const auto z0 = static_cast<uint32_t>(cell.y);
		const auto x1 = std::min(x0 + 1, samplesX - 1);
		const auto z1 = std::min(z0 + 1, samplesZ - 1);
		const auto t  = cell - glm::vec2(static_cast<float>(x0), static_cast<float>(z0));

		return glm::mix(
			glm::mix(at(x0, z0), at(x1, z0), t.x),
			glm::mix(at(x0, z1), at(x1, z1), t.x),
			t.y);
	}
}
