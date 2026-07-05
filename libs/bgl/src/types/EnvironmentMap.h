#pragma once
#include "resource/Texture.h"

namespace bgl
{
	// The three precomputed image-based-lighting resources Forward_PBR samples.
	// TextureHandle.idx is the bindless SRV index the shader reads.
	struct EnvironmentMap
	{
		TextureHandle irradiance;  // cubemap
		TextureHandle prefilter;   // cubemap (roughness mips)
		TextureHandle brdfLut;     // 2D LUT
	};
}
