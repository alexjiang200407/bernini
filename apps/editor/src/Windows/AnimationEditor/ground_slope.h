#pragma once

#include <bgl/types/GroundPlaneDesc.h>
#include <core/glm.h>

namespace editor
{
	/**
	 * The scene ground for a preview slope of `degrees`: the plane through the origin, rising
	 * toward +X for a positive slope.
	 *
	 * Free of the window because it is the one thing the slider cannot afford to get wrong and a
	 * test cannot reach the scene to check: a sign or an axis mistaken here plants every foot on a
	 * slope that leans the other way from the floor drawn under it.
	 */
	[[nodiscard]] bgl::GroundPlaneDesc
	GroundForSlope(float degrees);

	/**
	 * Where the preview's floor stands for the same slope: `AddPlaneGeom`'s quad, which lies in XY
	 * with its normal along +Z, laid flat and then tilted so its up is exactly GroundForSlope's
	 * normal. The two are derived from one rotation so they cannot disagree.
	 */
	[[nodiscard]] glm::mat4
	FloorTransformForSlope(float degrees);
}
