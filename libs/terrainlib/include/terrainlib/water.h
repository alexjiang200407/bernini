#pragma once
#include <assetlib_structs/Heightfield.h>
#include <terrainlib/types/TerrainWater.h>
#include <terrainlib/types/WaterDesc.h>

namespace terrain
{
	/**
	 * Cuts `desc`'s rivers and lakes into `field`, in place, and returns the water standing in
	 * them. Rivers follow the way water drains -- every sample toward its lowest neighbour on the
	 * ground filled from the field's edge, so a hollow drains over its rim -- from where enough
	 * ground drains through, the larger branch of each confluence first; each course is smoothed,
	 * its surface never rises downstream and lies no higher than the ground across its width, and
	 * its channel is dug below that surface with banks back up to the ground. Lakes are dug on the
	 * flattest low wet ground, each level with the lowest point of its rim, and a river through one
	 * runs at its level inside it. Any heightfield, generated or not; the field is requantised to
	 * the range it holds after the cut.
	 *
	 * Deterministic from the field and `desc`; linear in the samples but for one heap over them.
	 *
	 * @pre `field` holds `samplesX * samplesZ` samples, at least 2 along each axis, and its
	 *      cellSize is finite and positive.
	 * @throws std::runtime_error naming the first field of `desc` outside the range its comment
	 *         gives: a size, width, depth or speed not finite and positive (an area of 0 excepted),
	 *         a minimum past its maximum, or a shore noise outside [0, 1).
	 */
	[[nodiscard]] TerrainWater
	CarveWater(assetlib::Heightfield& field, const WaterDesc& desc);
}
