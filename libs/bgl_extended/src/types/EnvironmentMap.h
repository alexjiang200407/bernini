#pragma once
#include <bgpu/resource/Srv.h>

namespace bgl
{
	// The three precomputed image-based-lighting resources programs.forward.PBR samples.
	struct EnvironmentMap
	{
		bgpu::SrvHandle irradiance;  // cubemap
		bgpu::SrvHandle prefilter;   // cubemap (roughness mips)
		bgpu::SrvHandle brdfLut;     // 2D LUT
	};
}
