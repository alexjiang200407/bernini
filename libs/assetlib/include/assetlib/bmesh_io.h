#pragma once
#include <assetlib/bmesh/Bmesh.h>

namespace assetlib::bmesh
{
	/** Serializes the geometry and hierarchy of `mesh` into the versioned container byte stream. */
	[[nodiscard]] std::vector<std::byte>
	serialize(const BMesh& mesh);

	/**
	 * Reconstructs a BMesh from a container byte stream. The resulting `textures` are empty since
	 * textures are not part of the container.
	 *
	 * @throws std::runtime_error on bad magic, unsupported version, or a truncated / malformed stream.
	 */
	[[nodiscard]] BMesh
	deserialize(std::span<const std::byte> bytes);

	/**
	 * Writes the geometry and hierarchy of `mesh` to `path` as a `.bmesh` container. Textures are not
	 * written; use writeTextures for those.
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
	 * Writes each detached texture in `mesh` into `outDir` as a standalone `.dds` file named after the
	 * texture (falling back to `texN.dds`).
	 *
	 * @throws std::runtime_error if a file cannot be written.
	 */
	void
	writeTextures(const BMesh& mesh, const std::filesystem::path& outDir);
}
