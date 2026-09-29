#pragma once
#include <core/glm.h>
#include <core/math.h>
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

	/**
	 * Where slot `slot` of a group of `agentCount` agents stands under `orders`: the CPU half of the
	 * crowd's kernels, which must stay the same function. Slots fill ranks front to back and each
	 * rank from the facing's left, `(-facing.y, facing.x)`, to its right; the block is centred on the
	 * goal, and each rank across the facing, so a short last rank stands centred behind the others.
	 *
	 * @pre `slot < agentCount` and `orders` valid (GroupOrders).
	 */
	[[nodiscard]] inline glm::vec2
	SlotPosition(const GroupOrders& orders, uint32_t agentCount, uint32_t slot) noexcept
	{
		const uint32_t frontage  = orders.formation.frontage;
		const uint32_t rankCount = core::div_ceil(agentCount, frontage);
		const uint32_t rank      = slot / frontage;
		const uint32_t file      = slot % frontage;
		const uint32_t rankLength =
			rank + 1 < rankCount ? frontage : agentCount - (rank * frontage);

		const glm::vec2 front = glm::normalize(orders.facing);
		const glm::vec2 left  = glm::vec2(-front.y, front.x);

		const float ahead = ((static_cast<float>(rankCount - 1) * 0.5f) - static_cast<float>(rank));
		const float across =
			((static_cast<float>(rankLength - 1) * 0.5f) - static_cast<float>(file));
		return orders.goal + (orders.formation.spacing * ((ahead * front) + (across * left)));
	}
}
