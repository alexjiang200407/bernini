#pragma once
#include <core/glm.h>
#include <terrainlib/types/TerrainLayer.h>
#include <vector>

namespace terrain
{
	/**
	 * One river's course, from its head to its mouth: where it meets the field's edge, or the river
	 * it joins, whose point there it ends on. Positions are in metres from the field's sample
	 * (0, 0), and heights in the field's own frame, as `minHeight` is.
	 */
	struct WaterRiver
	{
		std::vector<glm::vec2> course;
		std::vector<float> surface;  // the water's height at each point, never rising downstream
		std::vector<float> width;    // metres, at each point
	};

	/** One lake: where its middle is, how far its shore reaches, and the height it stands at. */
	struct WaterLake
	{
		glm::vec2 centre = glm::vec2(0.0f);  // metres from the field's sample (0, 0)
		float     radius = 0.0f;             // metres, before the shore's noise
		float     level  = 0.0f;             // in the field's own frame
	};

	/**
	 * The water a field holds, each layer laid as its heightfield is. A sample is wet where its
	 * `depth` is above 0; `surface` is the water's height there, and the ground's own on dry
	 * ground, in the field's own frame -- a world height is the field's origin's y plus it, as
	 * HeightAt reads the ground.
	 */
	struct TerrainWater
	{
		TerrainLayer surface;
		TerrainLayer depth;  // metres of water over the ground; 0 where it is dry

		// The way the water runs, in metres a second along x and z: down a river's course, and 0
		// on a lake and on dry ground.
		TerrainLayer flowX;
		TerrainLayer flowZ;

		// Metres to the nearest sample across the water's edge: positive on dry ground, negative
		// in the water. What lays a beach along a shore and keeps a wood back from it.
		TerrainLayer shore;

		std::vector<WaterRiver> rivers;
		std::vector<WaterLake>  lakes;
	};
}
