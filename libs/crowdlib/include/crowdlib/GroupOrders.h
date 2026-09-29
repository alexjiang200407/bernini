#pragma once
#include <core/glm.h>
#include <cstdint>

namespace crowd
{
	/**
	 * A block of ranks, filled front to back: `frontage` agents to a rank, `spacing` apart in both
	 * directions. The last rank holds what is left over. Both must be positive.
	 */
	struct Formation
	{
		uint32_t frontage = 0;
		float    spacing  = 0.0f;
	};

	/**
	 * Where a group is to stand: its formation centred on `goal`, the front rank facing `facing`.
	 * Both are on the ground plane, as world (x, z). `facing` need not be unit length, but its length
	 * must be positive and finite. Its agents walk there at `pace` times their type's preferred
	 * speed, which must be positive and finite; their maximum speed still caps it.
	 */
	struct GroupOrders
	{
		glm::vec2 goal   = glm::vec2(0.0f);
		glm::vec2 facing = glm::vec2(0.0f, 1.0f);
		Formation formation;
		float     pace = 1.0f;
	};
}
