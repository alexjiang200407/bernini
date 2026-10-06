#pragma once
#include <cstdint>
#include <vector>

namespace assetlib
{
	/**
	 * A regular grid of heights: `samplesX` by `samplesZ` samples `cellSize` apart, each a 16-bit
	 * share of `heightRange` above `minHeight`. Row-major with x fastest, so sample (x, z) is
	 * `heights[z * samplesX + x]`. What a terrain generator produces and the renderer takes; the
	 * container a terrain is stored as later is this, serialised.
	 */
	struct Heightfield
	{
		uint32_t samplesX = 0;
		uint32_t samplesZ = 0;

		// World units between neighbouring samples, the same along both axes.
		float cellSize = 1.0f;

		// A sample of 0 is minHeight, and one of 65535 is minHeight + heightRange.
		float minHeight   = 0.0f;
		float heightRange = 1.0f;

		std::vector<uint16_t> heights;  // samplesX * samplesZ
	};
}
