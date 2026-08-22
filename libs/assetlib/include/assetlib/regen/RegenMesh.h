#pragma once
#include <assetlib_structs/BMesh.h>

namespace assetlib
{
	/**
	 * A mesh read through the regeneration seam: the container as loaded or regenerated, and every
	 * document binding naming a submesh the mesh does not have -- reported, never guessed at (the
	 * editor warns, `migrate` fails the file, `pack` fails the pack).
	 */
	struct RegenMesh
	{
		BMesh                    mesh;
		std::vector<std::string> unboundBindings;
	};
}
