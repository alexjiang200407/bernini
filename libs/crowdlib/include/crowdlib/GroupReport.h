#pragma once
#include <core/glm.h>
#include <cstdint>

namespace crowd
{
	/** What one completed tick measured of one group: aggregates over its agents, never an agent. */
	struct GroupReport
	{
		uint64_t  tick         = 0;
		uint32_t  agentCount   = 0;
		glm::vec2 meanPosition = glm::vec2(0.0f);
		glm::vec2 meanFacing   = glm::vec2(0.0f, 1.0f);
	};
}
