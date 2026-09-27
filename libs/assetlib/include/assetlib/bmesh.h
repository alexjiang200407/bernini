#pragma once
#include <assetlib_structs/BMeshImport.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace assetlib
{
	struct BMesh;

	/** References resolved from a mesh source's import document. */
	struct MeshRefs
	{
		std::vector<std::string> materials;

		std::string skeleton;  // empty for a static mesh
	};

	/** Copies cooked geometry, original material slot indices and named grass from an import. */
	[[nodiscard]] BMesh
	toBMesh(const imp::BMeshImport& mesh);

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
	 * The mesh's geometry as one number: the vertex blob and the entry and submesh tables that
	 * address it. Everything a measurement taken over posed vertices reads, and nothing that cannot
	 * move one -- materials, names and the rig are all outside it.
	 *
	 * Hashed once at cook time into `BMesh::geometrySignature` and read back from there, because
	 * this walks the whole blob and its callers are on a load path. Call it directly only to
	 * produce that field, or where a mesh carries a zero one.
	 */
	[[nodiscard]] uint64_t
	geometrySignature(const BMesh& mesh) noexcept;

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
