#pragma once
#include <terrainlib/TerrainLayer.h>

namespace terrain
{
	/**
	 * Where each kind of thing stands on a field, one layer per kind laid as its heightfield is, so
	 * a channel painted by hand later replaces one kind without touching the others. A mask is hard:
	 * every value is 0 or 1, and every region in it is at least the area its rule asked for.
	 */
	struct TerrainMasks
	{
		// 1 inside a wood.
		TerrainLayer forest;

		// Inside a wood, the distance in metres from the sample to the nearest sample outside it; 0
		// outside. What sizes a tree by how deep in its wood it stands.
		TerrainLayer forestDepth;

		// 1 where rock breaks through the ground.
		TerrainLayer rock;

		// 1 where water stands or runs: a lake deep enough, or a channel enough ground drains
		// through. Nothing else of the masks lies on it.
		TerrainLayer water;
	};
}
