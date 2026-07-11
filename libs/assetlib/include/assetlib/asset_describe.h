#pragma once
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>

namespace assetlib
{
	/**
	 * Renders the contents of an asset as human-readable text -- the counterpart of writeObj for the
	 * non-geometric properties. Both formats are opaque binary containers, so without this the only
	 * way to answer "what is actually in this file" is to hand-decode it against the serializer.
	 *
	 * The text is for a person, not a parser: it is not stable across versions, and nothing reads it
	 * back. `assetlib_cli describe` prints it; the editor can surface it in an asset inspector.
	 */

	/**
	 * Describes a mesh: its hierarchy and pool sizes, the material file it points each submesh at, and
	 * per-submesh geometry counts, vertex layout and bounds.
	 *
	 * @param verbose When true, every submesh is listed individually. When false only a summary and
	 *        the material table are emitted, which is what you want for a mesh with hundreds of them.
	 */
	[[nodiscard]] std::string
	describe(const BMesh& mesh, bool verbose = true);

	/**
	 * Describes a material: its mode, factors, the baked texture triplet, and the per-channel routing
	 * table with each route's bake provenance.
	 *
	 * Routes are reported against `dataRoot` when one is given: each routed source is stat'd and its
	 * live stamp compared with the one recorded at bake time, so a stale bake is visible here. Passing
	 * an empty path skips that and reports the recorded stamps alone.
	 */
	[[nodiscard]] std::string
	describe(const BMaterial& material, const std::filesystem::path& dataRoot = {});
}
