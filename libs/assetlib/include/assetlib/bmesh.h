#pragma once
#include <assetlib/cancel.h>
#include <assetlib_structs/BMeshImport.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace assetlib
{
	struct BMesh;

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
	 * through here rather than `store.Load<BMesh>`.
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
	 * *textures* are still extracted (see AssetStore::WriteTextures) -- they are what a material
	 * routes at.
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
	 * The file name AssetStore::WriteTextures gives each of an import's textures, parallel to
	 * `mesh.textures`: the image's own name, sanitised, `.ktx2`; `tex<index>` where the source
	 * names none, and an index suffix where two resolve alike (compared case-insensitively).
	 *
	 * The one place that rule lives -- a caller routing a material at one of those files comes
	 * through here. Why it is the image's name and not the index: docs/asset_standards.md.
	 */
	[[nodiscard]] std::vector<std::string>
	importedTextureFileNames(const imp::BMeshImport& mesh);

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
	 * The same question asked of one entry of `mesh.meshes`, which is the granularity a node needs:
	 * a document holds a skinned character and the static attachments that hang off its bones, and
	 * the two are placed by different rules (see the editor's GetInstanceTransform).
	 *
	 * @return false for a `meshIndex` that names no mesh, or a mesh whose submesh range is out of
	 *         bounds -- a caller asking about geometry that is not there gets "not skinned" rather
	 *         than a throw, since it has nothing to draw either way.
	 */
	[[nodiscard]] bool
	isSkinned(const BMesh& mesh, uint32_t meshIndex) noexcept;

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
