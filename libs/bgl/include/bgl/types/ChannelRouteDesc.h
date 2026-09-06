#pragma once
#include <bgl/TextureAssetHandle.h>
#include <cstdint>

namespace bgl
{
	struct ChannelRouteDesc
	{
		TextureAssetHandle texture;
		uint16_t           channel = 0;  // 0 = R, 1 = G, 2 = B, 3 = A
	};
}
