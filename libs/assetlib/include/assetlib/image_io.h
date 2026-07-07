#pragma once
#include <assetlib_structs/ImageData.h>

namespace assetlib
{
	/**
	 * Decodes a .ktx2 file (2D or cube map, with mips) into a GPU-uploadable ImageData.
	 *
	 * Image decoding lives in the asset library (alongside glTF image extraction); graphics
	 * code stays codec-free and just consumes the decoded ImageData through its
	 * texture-create path. The KTX2 container carries a Vulkan format that is translated to
	 * the raw DXGI format ImageData exposes; only uncompressed formats are handled today.
	 *
	 * @param path Path to a .ktx2 file.
	 * @throws std::runtime_error if the file cannot be read, decoded, or carries an unmapped format.
	 */
	[[nodiscard]] ImageData
	loadKTX2(const std::filesystem::path& path);

	/**
	 * Encodes an ImageData (its mips and array/cube faces) into a `.ktx2` file on disk. The inverse of
	 * loadKTX2; used to bake extracted asset textures to standalone files.
	 *
	 * @param srgb When true, the image's format is tagged with its sRGB Vulkan variant (same bits,
	 *        only the format field changes) so the GPU sampler decodes sRGB→linear on read. Use it for
	 *        color (base-color) textures; leave false for linear data (normal / ORM).
	 * @throws std::runtime_error if the file cannot be written or the format has no KTX2 mapping.
	 */
	void
	writeKTX2(const ImageData& image, const std::filesystem::path& path, bool srgb = false);
}
