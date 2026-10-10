#pragma once
#include <assetlib_structs/Heightfield.h>
#include <terrainlib/types/TerrainFields.h>

namespace terrain
{
	/**
	 * The fields of `field`'s ground, each laid as `field` is. Flow is accumulated by multiple flow
	 * directions: each sample, highest first, passes what drains through it to its lower
	 * neighbours in proportion to the slope down to each, so a valley gathers water along its
	 * whole floor rather than down one line. A sample with no lower neighbour keeps what reaches it.
	 * Lake depth is a priority flood from the field's edge (Barnes, Lehman and Mulla 2014): the
	 * edge is where water leaves.
	 *
	 * Deterministic, and linear in the samples but for one sort of them by height and one heap.
	 *
	 * @pre `field` holds `samplesX * samplesZ` samples, at least 2 along each axis, and its
	 *      cellSize is finite and positive.
	 */
	[[nodiscard]] TerrainFields
	DeriveFields(const assetlib::Heightfield& field);
}
