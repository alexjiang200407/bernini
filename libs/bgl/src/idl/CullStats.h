// THIS IS A FILE GENERATED FROM CullStats.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	struct CullStats
	{
		uint32_t tested;
		uint32_t frustumCulled;
	};

	static_assert(sizeof(CullStats) == 8);
	static_assert(offsetof(CullStats, tested) == 0);
	static_assert(offsetof(CullStats, frustumCulled) == 4);

}
