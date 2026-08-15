#pragma once
#include <assetlib/cancel.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMeshImport.h>

namespace assetlib
{
	/**
	 * Loads a glTF (.gltf / .glb) file and converts its geometry, materials, node hierarchy, skin and
	 * animations into a flattened imp::BMeshImport. Textures are decoded (detached) into
	 * `BMeshImport::textures`.
	 *
	 * A file with no skin imports as before, with an empty skeleton and no clips: the skin is what
	 * says which nodes are bones, and an animation that drives no bone is not a clip of a rig. A file
	 * with more than one skin is rejected -- one file is one rig here.
	 *
	 * @param path Path to a .gltf or .glb file.
	 * @param cancel Polled between meshes and between decoded images. The parse tinygltf does first is
	 *        one opaque call and cannot be interrupted, so a signalled token is not seen until it
	 *        returns.
	 * @param sampleRate Hz every clip is resampled to. The runtime indexes poses rather than searching
	 *        keyframes, so this is fixed at import and stored per clip.
	 * @return The imported document.
	 * @throws std::runtime_error if the file cannot be read, is not valid glTF, or uses an
	 *         unsupported feature (non-triangle primitives, sparse accessors, two skins, ...).
	 * @throws Cancelled if `cancel` is signalled.
	 */
	[[nodiscard]] imp::BMeshImport
	loadFromGltf(
		const std::filesystem::path& path,
		const CancelToken&           cancel     = {},
		float                        sampleRate = c_DefaultSampleRate);

	/** One entry of a glTF's material table, as probeGltfMaterials reports it. */
	struct GltfMaterial
	{
		std::string name;           // free text, and frequently empty
		bool        isPbr = false;  // whether an import derives a material from it
	};

	/**
	 * Reads `path`'s material table alone, so a caller can decide what to offer before committing to an
	 * import. No image is decoded: the parse runs with a stubbed image loader, which is what makes this
	 * cheap enough to call from a UI thread. The file is still read in full, so the cost is its size.
	 *
	 * @return The table in file order, index for index with the `imp::BMeshImport::materials` that
	 *         loading the same file produces -- which is what lets a caller name the files an import
	 *         will write before it runs.
	 * @throws std::runtime_error if the file cannot be read or is not valid glTF.
	 */
	[[nodiscard]] std::vector<GltfMaterial>
	probeGltfMaterials(const std::filesystem::path& path);
}
