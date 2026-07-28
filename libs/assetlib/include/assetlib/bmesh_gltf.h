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

	/** What a file's material table holds, without the cost of importing it. See probeGltfMaterials. */
	struct GltfMaterialProbe
	{
		size_t materialCount    = 0;
		size_t pbrMaterialCount = 0;
	};

	/**
	 * Reads `path`'s material table alone, so a caller can decide what to offer before committing to an
	 * import. No image is decoded: the parse runs with a stubbed image loader, which is what makes this
	 * cheap enough to call from a UI thread. The file is still read in full, so the cost is its size.
	 *
	 * @throws std::runtime_error if the file cannot be read or is not valid glTF.
	 */
	[[nodiscard]] GltfMaterialProbe
	probeGltfMaterials(const std::filesystem::path& path);
}
