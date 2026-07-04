#pragma once
#include <assetlib/bmesh/Bmesh.h>

namespace assetlib::bmesh
{
	/**
	 * Loads a glTF (.gltf / .glb) file and converts its geometry and node hierarchy into a BMesh.
	 * glTF materials and animations are ignored; textures are extracted (detached) into
	 * `BMesh::textures`.
	 *
	 * @param path Path to a .gltf or .glb file.
	 * @return The imported document.
	 * @throws std::runtime_error if the file cannot be read, is not valid glTF, or uses an
	 *         unsupported feature (non-triangle primitives, sparse accessors, ...).
	 */
	[[nodiscard]] BMesh
	loadFromGltf(const std::filesystem::path& path);
}
