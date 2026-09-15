#pragma once

namespace bgl
{
	/**
	 * The soft contact disc a placement casts on the static geometry beneath it: darkest under
	 * the placement, alpha falling off radially, fading as the gap between the placement and each
	 * receiving pixel grows. Not a lighting term -- the disc sits directly beneath the placement
	 * whatever the sun.
	 */
	struct BlobShadowDesc
	{
		// Disc radius in world units, multiplied by the placement's uniform scale.
		float radius = 1.0f;

		// Opacity at the disc's centre when the placement touches the receiving surface, in [0, 1].
		float intensity = 0.75f;

		// Height above the receiving surface at which the disc has fully faded out. World units.
		float fadeHeight = 2.0f;
	};
}
