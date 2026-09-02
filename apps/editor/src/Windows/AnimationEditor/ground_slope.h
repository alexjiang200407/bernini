#pragma once

#include <bgl/types/GroundPlaneDesc.h>
#include <core/glm.h>

namespace editor
{
	/**
	 * The scene ground for a preview slope of `degrees`, rising toward the horizontal direction
	 * `headingDegrees` turns +X to, about +Y: the plane through the origin.
	 *
	 * A heading and not a fixed axis because nothing in the path knows which way a rig moves --
	 * the test coyote runs along +Z -- and a slope across its stride shows a foot on a side-slope,
	 * where the one a person means is up the hill.
	 *
	 * Free of the window because it is the one thing the sliders cannot afford to get wrong and a
	 * test cannot reach the scene to check: a sign or an axis mistaken here plants every foot on a
	 * slope that leans the other way from the floor drawn under it.
	 */
	[[nodiscard]] bgl::GroundPlaneDesc
	GroundForSlope(float degrees, float headingDegrees = 0.0f);

	/**
	 * Where the preview's floor stands for the same slope and heading: `AddPlaneGeom`'s quad,
	 * which lies in XY with its normal along +Z, laid flat and then tilted so its up is exactly
	 * GroundForSlope's normal. The two are derived from one rotation so they cannot disagree.
	 */
	[[nodiscard]] glm::mat4
	FloorTransformForSlope(float degrees, float headingDegrees = 0.0f);
}
