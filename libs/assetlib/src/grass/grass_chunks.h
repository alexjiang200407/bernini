#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace assetlib
{
	struct BGrassFields;
	struct GrassClump;

	/**
	 * Appends one field of `clumps` growing on mesh `mesh`, named as a `.bimport` binds it, and
	 * unbound. The clumps are reordered along a Morton curve over the field's bounds before they
	 * are cut into chunks of `c_GrassClumpsPerChunk`, so a chunk is a patch of ground and its sphere
	 * is tight; in the order a DCC exported them, a chunk could span the whole field and cull
	 * nothing. The order is a function of the positions alone, so a re-import writes the same
	 * bytes.
	 *
	 * @pre `clumps` is not empty.
	 */
	void
	appendGrassField(
		BGrassFields&           grass,
		std::vector<GrassClump> clumps,
		uint32_t                mesh,
		std::string             name);
}
