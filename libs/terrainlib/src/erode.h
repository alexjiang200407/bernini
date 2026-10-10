#pragma once
#include <cstdint>
#include <span>
#include <terrainlib/types/ErosionDesc.h>

namespace terrain
{
	/**
	 * @throws std::runtime_error naming the first field of `desc` outside the range its comment
	 *         gives.
	 */
	void
	ValidateErosion(const ErosionDesc& desc);

	/**
	 * Joins the closed hollows of `heights` to the field's edge where a channel no deeper than
	 * `depth` can: a priority flood from the edge (Barnes, Lehman and Mulla 2014) that, entering
	 * a hollow, cuts the way it came down below the hollow's floor, as least-cost breaching does
	 * (Lindsay 2016), and leaves any hollow that would need a deeper cut to hold water.
	 * A cut is a V whose banks rise gently, never steeper than `talus`, rise over run.
	 */
	void
	BreachBasins(
		std::span<float> heights,
		uint32_t         samplesX,
		uint32_t         samplesZ,
		float            cellSize,
		float            depth,
		float            talus);

	/**
	 * Erodes `heights`, world heights in metres laid as a heightfield's samples, in place, as
	 * `desc` says. The field's volume is kept: what a droplet carries is laid down where it
	 * stops, and thermal erosion moves material between neighbours. The result depends on the
	 * heights, the desc and `seed` alone, never on how the work is scheduled across threads.
	 *
	 * @pre `heights` holds `samplesX * samplesZ` finite values, both at least 2, `cellSize` is
	 *      positive, and `desc` passed ValidateErosion.
	 */
	void
	Erode(
		std::span<float>   heights,
		uint32_t           samplesX,
		uint32_t           samplesZ,
		float              cellSize,
		const ErosionDesc& desc,
		uint32_t           seed);
}
