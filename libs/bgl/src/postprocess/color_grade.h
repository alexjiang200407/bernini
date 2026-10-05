#pragma once

#include <core/glm.h>

namespace bgl
{
	/**
	 * The per-channel CAT02 LMS scale that white-balances by `temperature` and `tint`: a von Kries
	 * adaptation from the daylight-locus white they name to D65. Exactly one at zero.
	 */
	[[nodiscard]] glm::vec3
	WhiteBalanceLmsScale(float temperature, float tint) noexcept;
}
