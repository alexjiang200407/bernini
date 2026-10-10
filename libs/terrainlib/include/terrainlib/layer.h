#pragma once
#include <core/glm.h>
#include <terrainlib/types/TerrainLayer.h>

namespace terrain
{
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
