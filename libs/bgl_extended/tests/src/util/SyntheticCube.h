#pragma once

#include <assetlib_structs/ImageData.h>
#include <cstdint>

namespace bgl::test
{
	/**
	 * A black float cube map: `faceSize` square, one mip, six faces, with the subresource table the
	 * loader reads already filled in.
	 *
	 * Black rather than merely allocated, and that is the whole reason this is not two lines at the
	 * call site: `core::fixed_buffer` allocates for overwrite, so a cube built without the fill
	 * carries whatever was in that memory. A caller that writes every texel afterwards never notices;
	 * one that uses the cube as it stands convolves stale heap into a blindingly bright environment,
	 * which looks like a lighting bug rather than an uninitialised buffer.
	 */
	[[nodiscard]] assetlib::ImageData
	MakeBlackFloatCube(uint32_t faceSize);
}
