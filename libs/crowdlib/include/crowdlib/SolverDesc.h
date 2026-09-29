#pragma once
#include <cstdint>

namespace crowd
{
	/**
	 * How hard the crowd's constraints are enforced each tick. The constraints themselves and their
	 * order are the crowd's; these tune them. The defaults are GPU Zen 3 ch. 13's, tuned for its
	 * 0.01 s tick (its avoidance threshold of 20 read as ticks), so a longer tick wants them retuned.
	 */
	struct SolverDesc
	{
		// Constraint passes per tick; at least 1.
		uint32_t iterations = 3;

		// The share of an agent's last velocity kept when it re-plans toward its goal, in [0, 1).
		float velocityInertia = 0.01f;

		// How far ahead, in seconds, agents avoid a predicted collision; 0 turns avoidance off.
		float avoidanceHorizon = 0.2f;

		// Each in [0, 1]: the share of a violation one iteration corrects.
		float avoidanceStiffness = 0.01f;
		float cohesionStiffness  = 0.01f;
	};
}
