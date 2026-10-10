#pragma once
#include <core/glm.h>
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

	/**
	 * The value of `layer` at world `xz`, laid with sample (0, 0) at `origin`'s x and z as
	 * terrain::HeightAt lays a heightfield: bilinear between samples and clamped at the edge.
	 *
	 * @pre `layer` holds `samplesX * samplesZ` values, at least one along each axis, its cellSize is
	 *      finite and positive, and `origin` and `xz` are finite.
	 */
	[[nodiscard]] float
	LayerAt(const TerrainLayer& layer, const glm::vec3& origin, glm::vec2 xz) noexcept;
}
