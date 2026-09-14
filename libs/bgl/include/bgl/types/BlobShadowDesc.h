#pragma once

namespace bgl
{
	/**
	 * The soft contact disc a placement casts on the scene's ground plane: darkest under the
	 * placement, alpha falling off radially, shrinking and fading as the placement rises. Not a
	 * lighting term -- the disc sits directly beneath the placement whatever the sun.
	 */
	struct BlobShadowDesc
	{
		// Disc radius in world units, multiplied by the placement's uniform scale.
		float radius = 1.0f;

		// Opacity at the disc's centre when the placement touches the ground, in [0, 1].
		float intensity = 0.75f;

		// Height above the ground at which the disc has fully faded out. World units.
		float fadeHeight = 2.0f;
	};
}
