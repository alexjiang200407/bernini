#pragma once
#include <assetlib_structs/Mesh.h>
#include <cstdint>
#include <vector>

namespace assetlib
{
	/** A mesh container's baked impostors: one record a mesh that has one, and the atlases they address. */
	struct MeshImpostors
	{
		std::vector<MeshImpostor> records;  // in mesh order, at most one a mesh
		std::vector<uint8_t>      texels;   // every record's two atlases, see MeshImpostor
	};
}
