#pragma once
#include "types/ComputeState.h"

namespace bgl
{
	class ComputePipeline;
	class Uniforms;

	struct ComputeState
	{
		core::SharedRef<ComputePipeline> pipeline;
		Uniforms*                        uniforms = nullptr;
	};
}