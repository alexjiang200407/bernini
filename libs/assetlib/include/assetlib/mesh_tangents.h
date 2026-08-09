#pragma once

namespace assetlib
{
	struct BMesh;
	struct Submesh;

	/** What generateTangents did, per submesh. */
	struct TangentGenResult
	{
		uint32_t generated = 0;  // gained a tangent attribute
		uint32_t kept      = 0;  // already had one, and was left alone
		uint32_t skipped   = 0;  // cannot have one derived: no normals, no UVs, or no triangles
	};

	/**
	 * Derives a per-vertex tangent basis for every submesh of `mesh` that has none, rewriting its
	 * vertex layout and interleaved blob in place.
	 *
	 * A normal map is authored in a tangent frame, and the shader reconstructs that frame from the
	 * vertex tangent -- so a mesh without one renders with its geometric normal and the map does
	 * nothing at all (see docs/asset_standards.md). Both importers call this, so a mesh only reaches
	 * disk without a tangent when it has no normals, no UVs or no triangles to derive one from.
	 *
	 * Derived the standard way: accumulate each triangle's UV-space basis onto its three vertices,
	 * then orthogonalise against the vertex normal. `w` is the bitangent's handedness, matching what
	 * the shader multiplies by. A vertex whose triangles cancel out -- a degenerate or mirrored UV
	 * shell -- falls back to an arbitrary axis perpendicular to its normal, which is no worse than
	 * the absent tangent it replaces.
	 *
	 * The vertex pool is rebuilt: a submesh that gains an attribute grows its stride, so every
	 * submesh after it in the pool moves and its `vertexByteOffset` is rewritten. Vertex count and
	 * order are untouched, so meshlets, indices and the material bindings all stay valid.
	 *
	 * @param mesh The mesh to rewrite. Untouched if every submesh is kept or skipped.
	 * @return How many submeshes gained a tangent, already had one, or could not have one derived.
	 * @throws std::runtime_error if a submesh's vertex blob does not match its declared layout.
	 */
	TangentGenResult
	generateTangents(BMesh& mesh);

	/** Whether `submesh` declares a tangent attribute. */
	[[nodiscard]] bool
	hasTangent(const Submesh& submesh) noexcept;
}
