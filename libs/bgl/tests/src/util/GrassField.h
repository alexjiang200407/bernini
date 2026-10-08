#pragma once
#include <assetlib_structs/BGrassFields.h>
#include <cstdint>

namespace bgl::test
{
	/**
	 * One field of `side` x `side` clumps `spacing` apart over a plane geom's XY, centred, growing
	 * along +Z (the plane's normal), chunked in rows the way a cook's Morton order would roughly.
	 * Attach it to a plane laid flat, and the field grows up.
	 */
	[[nodiscard]] assetlib::BGrassFields
	MakeGrassField(uint32_t side, float spacing);
}
