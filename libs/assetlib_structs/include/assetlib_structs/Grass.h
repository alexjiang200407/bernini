#pragma once
#include <core/glm.h>
#include <cstdint>

namespace assetlib
{
	/**
	 * Clumps one cooked chunk holds at most: the unit the renderer frustum-culls and thins by
	 * distance, one amplification group apiece. A field's last chunk is short.
	 */
	constexpr uint32_t c_GrassClumpsPerChunk = 64;

	/**
	 * One point a clump of blades grows from, in the mesh's object space. Read from a glTF POINTS
	 * primitive; the renderer builds the blades from a hash of the clump's index, so nothing about
	 * an individual blade is stored.
	 */
	struct GrassClump
	{
		glm::vec3 position;

		// Multiplies the grass look's blade heights. 1 where the source carried none.
		float heightScale;

		// The unit normal of the surface the clump grows from: what the look's ground-normal blend
		// shades toward. Object-space +Y where the source carried none.
		glm::vec3 normal;

		// Linear RGBA multiplier on the look's tints. White where the source carried none.
		glm::u8vec4 color;
	};

	static_assert(sizeof(GrassClump) == 32);

	/**
	 * A run of at most `c_GrassClumpsPerChunk` consecutive clumps of one field, and the bound the
	 * renderer tests before building any of them.
	 *
	 * The sphere encloses the clump *positions* only: blade height and clump radius belong to the
	 * grass look, which the cook never sees, so the renderer inflates it by the look's tallest blade
	 * times `maxHeightScale`, plus the clump radius.
	 */
	struct GrassChunk
	{
		glm::vec3 boundingCenter;
		float     boundingRadius;
		uint32_t  firstClump;  // into BGrassFields::clumps
		uint32_t  clumpCount;
		float     maxHeightScale;
	};

	static_assert(sizeof(GrassChunk) == 28);

	/**
	 * One POINTS primitive of a mesh source: a field of clumps growing on mesh `mesh`, drawn with
	 * the grass document `BGrassFields::looks[look]` names.
	 */
	struct GrassField
	{
		uint32_t mesh;        // into the BMesh's meshes, cooked from the same source
		uint32_t look;        // into BGrassFields::looks
		uint32_t firstChunk;  // into BGrassFields::chunks
		uint32_t chunkCount;
	};

	static_assert(sizeof(GrassField) == 16);
}
