#pragma once
#include <assetlib_structs/ImageData.h>

namespace assetlib
{
	/**
	 * Decodes a .dds file (2D or cube map, with mips) into a GPU-uploadable ImageData.
	 *
	 * Image decoding lives in the asset library (alongside glTF image extraction); graphics
	 * code stays codec-free and just consumes the decoded ImageData through its
	 * texture-create path.
	 *
	 * @param path Path to a .dds file.
	 * @throws std::runtime_error if the file cannot be read or decoded.
	 */
	[[nodiscard]] ImageData
	loadDDS(const std::filesystem::path& path);

	/**
	 * Encodes an ImageData (its mips and array slices) back into a `.dds` file on disk. The inverse of
	 * loadDDS; used to bake extracted asset textures to standalone files.
	 *
	 * @throws std::runtime_error if the file cannot be written (or, off Windows, always).
	 */
	void
	writeDDS(const ImageData& image, const std::filesystem::path& path);
}
