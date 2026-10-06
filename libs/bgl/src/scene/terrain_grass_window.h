#pragma once
#include <bgl/types/TerrainGrassDesc.h>
#include <cmath>

namespace bgl
{
	/**
	 * Tiles along each side of the window around the camera a terrain layer of `tileSize` tiles
	 * grows a look fading by `fadeEnd` in: every tile a blade within the fade could stand on,
	 * whichever tile of its own the camera is in. A float, unclamped, so a caller can refuse a
	 * window wider than c_MaxTerrainGrassWindowTiles before it is counted.
	 */
	[[nodiscard]] inline float
	TerrainGrassWindowTiles(const float fadeEnd, const float tileSize) noexcept
	{
		return 2.0f * (std::ceil(fadeEnd / tileSize) + 1.0f) + 1.0f;
	}
}
