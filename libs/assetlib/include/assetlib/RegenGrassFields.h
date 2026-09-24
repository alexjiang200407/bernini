#pragma once
#include <assetlib_structs/BGrassFields.h>
#include <string>
#include <vector>

namespace assetlib
{
	/**
	 * A mesh source's grass read through the regeneration seam: the container as loaded or
	 * regenerated, with the import document's grass bindings applied, and every grass binding naming
	 * a field the source does not have -- reported, never guessed at, as RegenMesh reports a submesh.
	 */
	struct RegenGrassFields
	{
		BGrassFields             fields;
		std::vector<std::string> unboundBindings;
	};
}
