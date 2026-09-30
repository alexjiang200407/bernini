#pragma once
#include <crowdlib/GroupHandle.h>
#include <crowdlib/debug/AgentReadback.h>
#include <cstdint>
#include <span>

namespace crowd::debug
{
	/** A group's agents in a readback: `agents[first, first + count)`, in formation-slot order. */
	struct GroupAgents
	{
		GroupHandle group;
		uint32_t    first = 0;
		uint32_t    count = 0;
	};

	/**
	 * Every agent as one completed tick left it, including the agents of a group released since:
	 * its handle is then refused by the crowd, but the tick still measured it. The spans are valid
	 * until the crowd's next Step or Wait, or its release.
	 */
	struct CrowdReadback
	{
		uint64_t                       tick = 0;
		std::span<const AgentReadback> agents;
		std::span<const GroupAgents>   groups;
	};
}
