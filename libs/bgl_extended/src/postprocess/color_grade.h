#pragma once

#include <bgl/IRenderTarget.h>
#include <core/glm.h>

namespace bgl
{
	/** @throws GraphicsError naming the first field outside the range IRenderTarget documents. */
	void
	ValidateColorGradeSettings(const ColorGradeSettings& settings);

	/**
	 * The per-channel CAT02 LMS scale that white-balances by `temperature` and `tint`: a von Kries
	 * adaptation from the daylight-locus white they name to D65. Exactly one at zero.
	 */
	[[nodiscard]] glm::vec3
	WhiteBalanceLmsScale(float temperature, float tint) noexcept;
}
