#pragma once
#include <assetlib/cancel.h>
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
	 * @throws std::runtime_error if the file cannot be written, naming the OS's reason (an existing file
	 *         that is read-only or held open by another process, a missing parent directory, ...).
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
	 * Bakes a flattened import into its modular file form: the geometry is copied verbatim and every
	 * submesh arrives with no material (`Submesh::material` is c_InvalidIndex, `materials` is empty).
	 *
	 * **An import does not carry materials across.** A glTF's materials are PBR, which is that format's
	 * shading model and not necessarily the engine's, so deriving `.bmaterial` files from them would
	 * stamp glTF's model into the engine's own container. Materials are authored in the material editor
	 * and bound to a submesh by attachMaterial when saved. The import's *textures* are still extracted
	 * (see writeTextures) -- they are what a material routes at.
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
	 * Reports that `done` of `total` textures have been written. Called before each texture, so the
	 * first call is (0, total) and the last is (total - 1, total).
	 */
	using TextureProgressFn = std::function<void(size_t done, size_t total)>;

	/**
	 * Writes each detached texture in `mesh` into `outDir` as a standalone `.ktx2` file named `texN.ktx2`
	 * by index. These are the files a material, once authored, routes at.
	 *
	 * `mesh.materials` is read but not written out: a texture's colour space is not a property of the
	 * image, and the import's materials are the only record of which of them a base colour is (sRGB,
	 * hardware-decoded) as opposed to a normal or ORM map (linear). That is the one thing glTF's PBR
	 * materials are still used for -- see toBMesh on why nothing else about them survives the import.
	 *
	 * Each texture is Basis-UASTC supercompressed, which dominates the cost of an import -- pass
	 * `onProgress` to drive a progress bar. It is called on the calling thread.
	 *
	 * @param cancel Polled once per texture. A single encode cannot be interrupted, so the wait for a
	 *        signalled token is one texture long -- seconds, at 4K. Whatever was already written stays
	 *        on disk.
	 * @throws std::runtime_error if `outDir` cannot be created or a file cannot be written.
	 * @throws Cancelled if `cancel` is signalled.
	 */
	void
	writeTextures(
		const imp::BMeshImport&      mesh,
		const std::filesystem::path& outDir,
		const TextureProgressFn&     onProgress = {},
		const CancelToken&           cancel     = {});

	/**
	 * Bakes an import to disk under `outDir`: writes `<name>.bmesh` and one `texN.ktx2` per texture.
	 *
	 * No materials: the mesh lands with its submeshes unassigned, and the textures land beside it for a
	 * material to be authored against (see toBMesh).
	 *
	 * @throws std::runtime_error if `outDir` cannot be created or a file cannot be written.
	 * @throws Cancelled if `cancel` is signalled, in which case `outDir` holds a partial bake.
	 */
	void
	bake(
		const imp::BMeshImport&      mesh,
		const std::filesystem::path& outDir,
		std::string_view             name,
		const CancelToken&           cancel = {});

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
