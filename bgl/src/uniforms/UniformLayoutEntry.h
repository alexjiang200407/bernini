#pragma once

namespace bgl
{
	// Describes a single constant buffer parameter reflected from a pipeline's
	// linked shader program: its size, the slang type layout used to build the
	// CPU-side Uniforms mirror, and the root parameter slot it binds to.
	struct UniformLayoutEntry
	{
		uint32_t                     size;
		slang::TypeLayoutReflection* layout;
		uint32_t                     rootParamIndex = 0xFFFFFFFF;
	};
}
