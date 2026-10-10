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

	/** What a sample on the field's edge drains to: nothing, the water leaves there. */
	constexpr uint32_t c_NoSample = ~0u;

	/** What a priority flood from a field's edge finds; see FloodFromEdge. */
	struct Flood
	{
		// Each sample's height, every hollow filled to where it spills.
		std::vector<float> filled;

		// The sample the flood reached each from, which it drains to; c_NoSample on the edge.
		std::vector<uint32_t> receiver;

		// Every sample, in the order the flood reached it.
		std::vector<uint32_t> order;
	};

	/**
	 * A priority flood from the edge of `heights`, a grid of `samplesX` by `samplesZ` (Barnes, Lehman
	 * and Mulla 2014): water rises from the edge, lowest first and ties by index, and every sample it
	 * reaches stands at least as high as the water that reached it. A sample drains to the neighbour
	 * the flood reached it from -- its lowest by the ground as filled -- so a hollow drains over its
	 * rim and every sample reaches the edge.
	 *
	 * @pre `heights` holds `samplesX * samplesZ` values, both at least 1.
	 */
	[[nodiscard]] Flood
	FloodFromEdge(const std::vector<float>& heights, uint32_t samplesX, uint32_t samplesZ);
}
