#pragma once
#include <cstdint>
#include <vector>

namespace terrain
{
	/**
	 * One value per sample of a heightfield, laid exactly as its heights are: `samplesX` by
	 * `samplesZ` samples `cellSize` apart, row-major with x fastest, so value (x, z) stands where
	 * height (x, z) does. What a field derived from the ground (TerrainFields) and a mask over it
	 * (TerrainMasks) are made of, and what a renderer's terrain layer texture is uploaded from.
	 */
	struct TerrainLayer
	{
		uint32_t samplesX = 0;
		uint32_t samplesZ = 0;
		float    cellSize = 1.0f;

		std::vector<float> values;  // samplesX * samplesZ
	};
}
