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

		// Ticks of RenderAgent records the crowd keeps for a renderer (ICrowd::GetRenderRing): 0 for
		// none, else at least maxTicksInFlight + 3 -- the ticks in flight, the two a reader
		// interpolates between and the one before them its motion is taken from. Past that, each
		// tick is how far the crowd may run ahead of a reader before it stops stepping.
		uint32_t renderRingTicks = 0;

		SolverDesc solver;

		// Keeps every agent's position and facing for ICrowd::ReadDebugAgents, at a copy per tick.
		bool debugAgentReadback = false;
	};
}
