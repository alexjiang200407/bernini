#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace assetlib
{
	struct ResolvedMaterialOverride
	{
		uint32_t    submesh = 0;
		std::string name;
		std::string material;
	};

	/** Resolved from the import document; never serialized into cooked geometry. */
	struct MeshBindings
	{
		std::vector<std::string>
			submeshMaterials;  // Parallel to BMesh::submeshes; empty means unbound.
		std::vector<ResolvedMaterialOverride> materialOverrides;
		std::string                           skeleton;
		std::vector<std::string> grassLooks;  // Indexed by GrassField::look; empty means unbound.
	};
}
