#pragma once
#include <assetlib_structs/Heightfield.h>
#include <core/glm.h>

namespace terrain
{
	/**
	 * The world height of `field` at world `xz`, laid with sample (0, 0) at `origin` as
	 * bgl::TerrainDesc lays it -- a sample of 0 at `origin.y + minHeight` -- bilinear between samples
	 * and clamped at the field's edge. The CPU
	 * twin of the shaders' `TerrainHeightAt`, so whatever the CPU stands on the ground stands where
	 * the terrain is drawn.
	 *
	 * @pre `field` holds `samplesX * samplesZ` samples, at least one along each axis, its cellSize
	 *      is finite and positive, and `origin` and `xz` are finite.
	 */
	[[nodiscard]] float
	HeightAt(const assetlib::Heightfield& field, const glm::vec3& origin, glm::vec2 xz) noexcept;
}
