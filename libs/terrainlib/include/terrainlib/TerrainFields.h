#pragma once
#include <assetlib_structs/Heightfield.h>
#include <terrainlib/TerrainLayer.h>

namespace terrain
{
	/**
	 * What the shape of the ground says about each of its samples, laid as its heightfield is:
	 * what decides where woods grow and rocks break through, and what a game reads to stand
	 * something on the ground for a reason.
	 */
	struct TerrainFields
	{
		// Rise over run, never negative: 1 is a 45 degree slope.
		TerrainLayer slope;

		// The Laplacian of the height, per metre: positive in a hollow, negative on a ridge or crest,
		// zero on a plane however steep.
		TerrainLayer curvature;

		// The area that drains through the sample, in square metres, its own cell included: water
		// gathering downhill, so a valley floor holds the most and a crest only its own cell.
		TerrainLayer flow;

		// How wet the ground is, in [0, 1]: the flow on a log scale, 0 where no more than 100 square
		// metres drain through and 1 from a square kilometre up, so it reads the same on any field.
		TerrainLayer wetness;

		// The metres of water standing on the sample were every depression filled to the height it
		// spills over at; 0 wherever water runs off. Where a lake would lie.
		TerrainLayer lakeDepth;
	};

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
