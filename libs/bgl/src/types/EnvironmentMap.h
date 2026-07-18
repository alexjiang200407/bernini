#pragma once
#include "resource/Texture.h"

namespace bgl
{
	// The three precomputed image-based-lighting resources Forward_PBR samples. Each RHI handle's
	// slot becomes a descriptor handle the shader samples through.
	struct EnvironmentMap
	{
		TextureHandle irradiance;  // cubemap
		TextureHandle prefilter;   // cubemap (roughness mips)
		TextureHandle brdfLut;     // 2D LUT
	};
}
