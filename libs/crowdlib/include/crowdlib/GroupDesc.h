#pragma once
#include <crowdlib/GroupOrders.h>
#include <cstdint>

namespace crowd
{
	/**
	 * A group as ICrowd::CreateGroup spawns it: `agentCount` agents of the crowd's type at index
	 * `agentType`, standing in `orders` as if they had already carried them out, and holding there
	 * until given others.
	 */
	struct GroupDesc
	{
		uint32_t    agentType  = 0;
		uint32_t    agentCount = 0;
		GroupOrders orders;
	};
}
