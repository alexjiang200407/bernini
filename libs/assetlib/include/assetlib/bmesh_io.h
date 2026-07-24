#pragma once
#include <assetlib/cancel.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>

namespace assetlib
{
	/**
	 * Serializes the geometry, hierarchy, material paths and skeleton path of `mesh` into the versioned
	 * container.
	 *
	 * @throws std::runtime_error if `mesh` carries joint indices but names no skeleton -- see isSkinned.
	 *         Refused here rather than written, because nothing that reads the file afterwards can tell
	 *         a joint index that resolves to nothing from one that does not.
	 */
	[[nodiscard]] std::vector<std::byte>
	serialize(const BMesh& mesh);

	/**
	 * Reconstructs a BMesh from a container byte stream.
	 *
	 * @throws std::runtime_error on bad magic, unsupported version, a truncated / malformed stream, or a
	 *         mesh that carries joints and names no skeleton.
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

	/** Every asset a `.bmesh` names. See loadMeshRefs. */
	struct MeshRefs
	{
		/**
		 * As stored, in `mesh.materials` order, duplicates and all: a submesh slot can legitimately
		 * repeat a path (see attachMaterial).
		 */
		std::vector<std::string> materials;

		std::string skeleton;  // empty for a static mesh
	};

	/**
	 * What `path` references, read without deserializing its geometry: the header, the chunk table and
	 * the two reference chunks alone. Those are a few hundred bytes in a file of many megabytes, so a
	 * caller surveying every mesh in a project -- which is what a reference scan does -- must come
	 * through here rather than load().
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] MeshRefs
	loadMeshRefs(const std::filesystem::path& path);

	/**
	 * Bakes a flattened import into its modular file form: the geometry is copied verbatim and every
	 * submesh arrives with no material (`Submesh::material` is c_InvalidIndex, `materials` is empty).
	 *
	 * **This does not carry materials across, and nothing in assetlib does.** A glTF's materials are
	 * PBR, which is that format's shading model and not necessarily the engine's, so deriving
	 * `.bmaterial` files here would stamp glTF's model into the engine's own container for every
	 * caller -- including `assetlib_cli bake`, which has no user to ask. attachMaterial is the only
	 * thing that binds a material, and a caller that wants the glTF's has to derive them and call it:
	 * the editor's import does exactly that, behind a checkbox, for the PBR ones alone. The import's
	 * *textures* are still extracted (see writeTextures) -- they are what a material routes at.
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
	 * The name writeTextures gives the `index`-th texture of an import. A caller that must name one of
	 * those files -- to route a material at it -- has to come through here rather than spell the
	 * convention out a second time.
	 */
	[[nodiscard]] std::string
	textureFileName(size_t index);

	/** The names bake gives the rig it writes beside a `<name>.bmesh`. */
	[[nodiscard]] std::string
	skeletonFileName(std::string_view name);

	[[nodiscard]] std::string
	animationFileName(std::string_view name);

	/**
	 * Whether any submesh carries joint indices. Such a mesh is only drawable against a skeleton, so
	 * one that names none is a mesh whose joint indices mean nothing.
	 */
	[[nodiscard]] bool
	isSkinned(const BMesh& mesh) noexcept;

	/**
	 * The name at `offset` in a mesh's `stringPool` (BMesh's or BMeshImport's -- they pool names the
	 * same way). Empty for offset 0, which is the empty string, or for an offset past the pool.
	 */
	[[nodiscard]] std::string
	nameFromPool(const std::vector<char>& pool, uint32_t offset);

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
	 * Bakes an import to disk under `outDir`: writes `<name>.bmesh`, one `texN.ktx2` per texture, and
	 * -- when the source carried a skin -- `<name>.bskel` and `<name>.banim`.
	 *
	 * No materials: the mesh lands with its submeshes unassigned, and the textures land beside it for a
	 * material to be authored against (see toBMesh). The rig is the exception, because unlike a material
	 * it is not an authoring choice: joint indices are meaningless without the skeleton they were
	 * remapped into, so the two are written together or the mesh is not skinned at all.
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
