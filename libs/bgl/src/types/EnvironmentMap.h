#pragma once
#include "resource/Srv.h"

namespace bgl
{
	// The three precomputed image-based-lighting resources programs.forward.PBR samples.
	struct EnvironmentMap
	{
		SrvHandle irradiance;  // cubemap
		SrvHandle prefilter;   // cubemap (roughness mips)
		SrvHandle brdfLut;     // 2D LUT
	};
}
