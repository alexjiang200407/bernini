#pragma once
#include <assetlib/cancel.h>
#include <assetlib_structs/BMeshImport.h>

namespace assetlib
{
	/**
	 * Loads a glTF (.gltf / .glb) file and converts its geometry, materials and node hierarchy into a
	 * flattened imp::BMeshImport. Textures are decoded (detached) into `BMeshImport::textures`;
	 * animations are ignored.
	 *
	 * @param path Path to a .gltf or .glb file.
	 * @param cancel Polled between meshes and between decoded images. The parse tinygltf does first is
	 *        one opaque call and cannot be interrupted, so a signalled token is not seen until it
	 *        returns.
	 * @return The imported document.
	 * @throws std::runtime_error if the file cannot be read, is not valid glTF, or uses an
	 *         unsupported feature (non-triangle primitives, sparse accessors, ...).
	 * @throws Cancelled if `cancel` is signalled.
	 */
	[[nodiscard]] imp::BMeshImport
	loadFromGltf(const std::filesystem::path& path, const CancelToken& cancel = {});
}
