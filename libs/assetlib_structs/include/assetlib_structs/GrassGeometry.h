#pragma once
#include <assetlib_structs/Grass.h>
#include <string>
#include <vector>

namespace assetlib
{
	struct NamedGrassField
	{
		std::string name;
		GrassField  field;
	};

	struct GrassGeometry
	{
		std::vector<NamedGrassField> fields;
		std::vector<GrassChunk>      chunks;
		std::vector<GrassClump>      clumps;
	};
}
