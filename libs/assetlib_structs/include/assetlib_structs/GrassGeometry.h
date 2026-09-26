#pragma once
#include <assetlib_structs/Grass.h>
#include <string>
#include <vector>

namespace assetlib
{
	struct GrassGeometry
	{
		std::vector<GrassField>  fields;
		std::vector<std::string> names;  // Parallel to fields; the names bindings address.
		std::vector<GrassChunk>  chunks;
		std::vector<GrassClump>  clumps;
	};
}
