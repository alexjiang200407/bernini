#pragma once
#include <crowdlib/AgentType.h>
#include <crowdlib/SolverDesc.h>
#include <cstdint>
#include <vector>

namespace crowd
{
	/**
	 * What a crowd is sized, clocked and solved by, fixed for its life. The capacities size its GPU
	 * state once, so a command that would exceed one is refused rather than grown into.
	 */
	struct CrowdDesc
	{
		std::vector<AgentType> agentTypes;

		uint32_t maxAgents           = 0;
		uint32_t maxGroups           = 0;
		uint32_t maxObstacleSegments = 0;

		// Every Step advances by this, never by a frame's delta.
		float tickSeconds = 1.0f / 30.0f;

		uint32_t maxTicksInFlight = 2;

		SolverDesc solver;

		// Keeps every agent's position and facing for ICrowd::ReadDebugAgents, at a copy per tick.
		bool debugAgentReadback = false;
	};
}
