#pragma once
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>

namespace assetlib
{
	/** Serializes the geometry, hierarchy and material paths of `mesh` into the versioned container. */
	[[nodiscard]] std::vector<std::byte>
	serialize(const BMesh& mesh);

	/**
	 * Reconstructs a BMesh from a container byte stream.
	 *
	 * @throws std::runtime_error on bad magic, unsupported version, or a truncated / malformed stream.
	 */
	[[nodiscard]] BMesh
	deserialize(std::span<const std::byte> bytes);

	/**
	 * Writes `mesh` to `path` as a `.bmesh` container. Only the mesh itself is written; the textures
	 * and materials it references are separate files (see writeTextures).
	 *
	 * @throws std::runtime_error if the file cannot be written.
	 */
	void
	save(const BMesh& mesh, const std::filesystem::path& path);

	/**
	 * Loads a `.bmesh` container previously written by save.
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] BMesh
	load(const std::filesystem::path& path);

	/**
	 * Bakes a flattened import into its modular file form: geometry is copied verbatim and each inline
	 * material becomes a `matN.bmaterial` file-path handle (Submesh::material indexes this list). Pair
	 * with writeMaterials + writeTextures (or just call bake) to emit the referenced files.
	 */
	[[nodiscard]] BMesh
	toBMesh(const imp::BMeshImport& mesh);

	/**
	 * Points submesh `submeshIndex` at the material file `relativePath` (relative to the project's data
	 * root, like every asset reference), adjusting `mesh.materials` and `Submesh::material` as needed.
	 * Used when an authoring tool saves a material and the mesh must reference it from then on.
	 *
	 * Material slots are shared: an import gives every submesh cut from the same source material the
	 * same index. So the submesh's existing slot is rewritten in place only when no other submesh uses
	 * it; otherwise the submesh moves to its own slot, reusing an entry that already holds
	 * `relativePath` rather than appending a duplicate. Sibling submeshes are never repointed.
	 *
	 * @return true if `mesh` changed, false if it already referenced that material (nothing to write).
	 * @throws std::runtime_error if `submeshIndex` is out of range.
	 */
	bool
	attachMaterial(BMesh& mesh, uint32_t submeshIndex, std::string_view relativePath);

	/**
	 * Writes each detached texture in `mesh` into `outDir` as a standalone `.ktx2` file named `texN.ktx2`
	 * by index. These are the texture files the baked `.bmaterial` files reference.
	 *
	 * @throws std::runtime_error if a file cannot be written.
	 */
	void
	writeTextures(const imp::BMeshImport& mesh, const std::filesystem::path& outDir);

	/**
	 * Writes each material in `mesh` into `outDir` as a `matN.bmaterial` file (matching the path handles
	 * toBMesh assembles), mapping each material's texture indices to the `texN.ktx2` names writeTextures
	 * emits.
	 *
	 * @throws std::runtime_error if a file cannot be written.
	 */
	void
	writeMaterials(const imp::BMeshImport& mesh, const std::filesystem::path& outDir);

	/**
	 * Bakes an import to disk under `outDir`: writes `<name>.bmesh`, one `matN.bmaterial` per material,
	 * and one `texN.ktx2` per texture. This is the complete modular form the runtime loads.
	 *
	 * @throws std::runtime_error if a file cannot be written.
	 */
	void
	bake(const imp::BMeshImport& mesh, const std::filesystem::path& outDir, std::string_view name);

	/**
	 * Writes `mesh` to `path` as a Wavefront `.obj` for inspection in an external model viewer -- a
	 * debugging aid for isolating a bad mesh format from a bad shader.
	 *
	 * @param fromMeshlets When true (default) the triangles are reconstructed from the meshlet clusters,
	 *        i.e. exactly the geometry the GPU draws, so a corrupt meshlet build is visible in the
	 *        viewer. When false the raw per-submesh index buffer is emitted instead, letting you compare
	 *        the source geometry against the meshletized form.
	 * @throws std::runtime_error if the file cannot be written.
	 */
	void
	writeObj(const BMesh& mesh, const std::filesystem::path& path, bool fromMeshlets = true);
}
