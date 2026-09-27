#pragma once
#include <assetlib_structs/Grass.h>
#include <assetlib_structs/SourceRef.h>
#include <string>
#include <vector>

namespace assetlib
{
	/**
	 * Decoded grass for import and renderer inputs. Cooked storage belongs to BMesh::grassFields.
	 *
	 * A field's chunks are a run of `chunks`, and each chunk's clumps a run of `clumps`.
	 */
	struct BGrassFields
	{
		// The `.bgrass` documents the fields are drawn with, by slot, as the `.bimport` bound them.
		std::vector<std::string> looks;

		std::vector<GrassField> fields;

		// Parallel to `fields`: the primitive each came from, named as a `.bimport` binds it --
		// `<mesh>`, or `<mesh>[p]` for a mesh of several primitives, exactly as a submesh is.
		std::vector<std::string> names;

		std::vector<GrassChunk> chunks;  // one bound per c_GrassClumpsPerChunk clumps
		std::vector<GrassClump> clumps;

		SourceRef source;  // the copied .glb this was derived from; empty key when never recorded
	};
}
