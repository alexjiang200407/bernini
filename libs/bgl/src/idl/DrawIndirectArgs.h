// THIS IS A FILE GENERATED FROM DrawIndirectArgs.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	struct DrawIndirectArgs
	{
		uint32_t vertexCount;
		uint32_t instanceCount;
		uint32_t firstVertex;
		uint32_t firstInstance;
	};

	static_assert(sizeof(DrawIndirectArgs) == 16);
	static_assert(offsetof(DrawIndirectArgs, vertexCount) == 0);
	static_assert(offsetof(DrawIndirectArgs, instanceCount) == 4);
	static_assert(offsetof(DrawIndirectArgs, firstVertex) == 8);
	static_assert(offsetof(DrawIndirectArgs, firstInstance) == 12);

}
