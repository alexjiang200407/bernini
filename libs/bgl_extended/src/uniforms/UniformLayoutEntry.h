#pragma once
#include "uniforms/ReflectedLayout.h"
#include <core/str/str.h>

namespace bgl
{
	// Describes a single constant buffer parameter reflected from a pipeline's linked
	// shader program: its size, the API-agnostic layout used to build the CPU-side
	// Uniforms mirror, and the root parameter slot it binds to. The shared layout is
	// cheap to copy (this struct is returned by value) and outlives the pipeline's
	// reflection object, so it can be serialized into the shader cache.
	struct UniformLayoutEntry
	{
		uint32_t                               size = 0;
		std::shared_ptr<const ReflectedLayout> layout;
		uint32_t                               rootParamIndex = 0xFFFFFFFF;
	};

	using UniformLayoutMap = core::str::unordered_str_map<UniformLayoutEntry>;
}
