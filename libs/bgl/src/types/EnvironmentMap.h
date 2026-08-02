#pragma once
#include "resource/Texture.h"

namespace bgl
{
	// The three precomputed image-based-lighting resources Forward_PBR samples.
	struct EnvironmentMap
	{
		// Asset handles, not TextureHandles: a texture carries no descriptor, and the asset handle
		// is what the scene stamped with its view's bindless index.
		TextureAssetHandle irradiance;  // cubemap
		TextureAssetHandle prefilter;   // cubemap (roughness mips)
		TextureAssetHandle brdfLut;     // 2D LUT
	};
}
