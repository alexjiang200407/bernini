#pragma once
#include "resource/Texture.h"

namespace bgl
{
	// The three precomputed image-based-lighting resources Forward_PBR samples.
	struct EnvironmentMap
	{
		TextureAssetHandle irradiance;  // cubemap
		TextureAssetHandle prefilter;   // cubemap (roughness mips)
		TextureAssetHandle brdfLut;     // 2D LUT
	};
}
