#pragma once
#include <assetlib_structs/ImageData.h>

namespace assetlib
{
	/**
	 * Decodes a tightly-packed RGBA8 image into a GPU-uploadable ImageData (with a generated mip
	 * chain). The result carries decoded pixels -- no handles -- so it slots straight into
	 * imp::BMeshImport::textures and is baked to a standalone `.dds` by writeTextures.
	 *
	 * @param rgba   width*height*4 bytes, row-major, no padding.
	 * @param width  Image width in pixels.
	 * @param height Image height in pixels.
	 * @throws std::runtime_error if `rgba` is too small or the encode fails (or, off Windows, always).
	 */
	[[nodiscard]] ImageData
	rgba8ToImage(std::span<const std::byte> rgba, uint32_t width, uint32_t height);
}
