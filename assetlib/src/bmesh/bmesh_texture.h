#pragma once
#include <assetlib/bmesh/Texture.h>

namespace assetlib::bmesh
{
	/**
	 * Encodes a tightly-packed RGBA8 image as an in-memory DDS (with a generated mip chain).
	 *
	 * @param rgba   width*height*4 bytes, row-major, no padding.
	 * @param width  Image width in pixels.
	 * @param height Image height in pixels.
	 * @param name   Identifying name carried into the returned Texture.
	 * @throws std::runtime_error if `rgba` is too small or the encode fails (or, off Windows, always).
	 */
	[[nodiscard]] Texture
	rgba8ToDds(std::span<const std::byte> rgba, uint32_t width, uint32_t height, std::string name);
}
