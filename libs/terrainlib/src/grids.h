#pragma once
#include <cstdint>
#include <vector>

namespace terrain
{
	/**
	 * For each sample of `mask` above one half, the distance in metres to the nearest sample at or
	 * below it, exactly (Felzenszwalb and Huttenlocher 2012); 0 elsewhere. With no sample at or
	 * below one half, a distance larger than any field.
	 *
	 * @pre `mask` holds `samplesX * samplesZ` values.
	 */
	[[nodiscard]] std::vector<float>
	DistanceInside(
		const std::vector<float>& mask,
		uint32_t                  samplesX,
		uint32_t                  samplesZ,
		float                     cellSize);

	/**
	 * The mean of `values`, a grid of `samplesX` by `samplesZ`, over the box `radius` samples
	 * around each sample, clipped to the grid, from a summed-area table.
	 */
	[[nodiscard]] std::vector<float>
	BoxMean(const std::vector<float>& values, uint32_t samplesX, uint32_t samplesZ, int radius);
}
