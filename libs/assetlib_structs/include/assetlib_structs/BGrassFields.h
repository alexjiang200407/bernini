#pragma once
#include <assetlib_structs/Grass.h>
#include <string>
#include <vector>

namespace assetlib
{
	/**
	 * The grass a mesh source grows: every POINTS primitive of a `.glb`, cooked beside its `.bmesh`
	 * from the same source and by the same import. A cache entry of its own rather than part of the
	 * mesh, so a change to how clumps are stored re-cooks grass and leaves every mesh alone.
	 *
	 * A field's chunks are a run of `chunks`, and each chunk's clumps a run of `clumps`.
	 */
	struct BGrassFields
	{
		// The `.bgrass` documents the fields are drawn with, by slot, as the `.bimport` bound them.
		std::vector<std::string> looks;

		std::vector<GrassField> fields;
		std::vector<GrassChunk> chunks;  // one bound per c_GrassClumpsPerChunk clumps
		std::vector<GrassClump> clumps;
	};
}
