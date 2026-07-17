#pragma once

namespace bgl
{
	struct SkyboxDesc
	{
		TextureAssetHandle skyboxCubeTex;
		uint32_t           mipLevel  = 0;
		float              exposure  = 1.0f;
		float              rotationY = 0.0f;
	};
}