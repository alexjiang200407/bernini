#pragma once
#include <core/glm.h>
#include <crowdlib/GroupHandle.h>
#include <cstdint>
#include <span>

namespace crowd::debug
{
	/** One agent: where it stands and the unit direction it faces, world (x, z). */
	struct AgentSample
	{
		glm::vec2 position = glm::vec2(0.0f);
		glm::vec2 facing   = glm::vec2(0.0f, 1.0f);
	};

	static_assert(sizeof(AgentSample) == 16);

	/** A group's agents in a snapshot: `agents[first, first + count)`, in formation-slot order. */
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
	struct AgentSnapshot
	{
		uint64_t                     tick = 0;
		std::span<const AgentSample> agents;
		std::span<const GroupAgents> groups;
	};
}
