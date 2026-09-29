#pragma once
#include <core/glm.h>

namespace crowd
{
	/**
	 * A wall no agent crosses, from `from` to `to` on the ground plane as world (x, z). An impassable
	 * area is its outline; a pillar may be a segment of zero length. Both ends must be finite.
	 */
	struct ObstacleSegment
	{
		glm::vec2 from = glm::vec2(0.0f);
		glm::vec2 to   = glm::vec2(0.0f);
	};
}
