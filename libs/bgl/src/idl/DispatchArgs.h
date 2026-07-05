// THIS IS A FILE GENERATED FROM DispatchArgs.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	struct DispatchArgs
	{
		uint32_t threadCountX;
		uint32_t threadCountY;
		uint32_t threadCountZ;
	};

	static_assert(sizeof(DispatchArgs) == 12);
	static_assert(offsetof(DispatchArgs, threadCountX) == 0);
	static_assert(offsetof(DispatchArgs, threadCountY) == 4);
	static_assert(offsetof(DispatchArgs, threadCountZ) == 8);

}
