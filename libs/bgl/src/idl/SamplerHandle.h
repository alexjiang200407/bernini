// THIS IS A FILE GENERATED FROM SamplerHandle.slang. DO NOT EDIT MANUALLY
#pragma once
#include "uniforms/DescriptorHandle.h"

namespace bgl::idl
{
	struct SamplerHandle
	{
		DescriptorHandle sampler;
	};

	static_assert(sizeof(SamplerHandle) == 8);
	static_assert(offsetof(SamplerHandle, sampler) == 0);

}
