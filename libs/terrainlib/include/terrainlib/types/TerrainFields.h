#pragma once
#include <terrainlib/types/TerrainLayer.h>

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
}
