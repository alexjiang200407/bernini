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
	 * material becomes a `.bmaterial` file-path handle. Pair with writeTextures to emit the referenced
	 * texture files.
	 *
	 * TODO: the referenced `.bmaterial` (and animation) files are not written yet; only their paths are
	 * assembled here, and they do not yet carry the mapped texture paths.
	 */
	[[nodiscard]] BMesh
	toBMesh(const imp::BMeshImport& mesh);

	/**
	 * Writes each detached texture in `mesh` into `outDir` as a standalone `.dds` file named `texN.dds`
	 * by index. These are the texture files the baked `.bmaterial` files will reference.
	 *
	 * @throws std::runtime_error if a file cannot be written.
	 */
	void
	writeTextures(const imp::BMeshImport& mesh, const std::filesystem::path& outDir);
}
