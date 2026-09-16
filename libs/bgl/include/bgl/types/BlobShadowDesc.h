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

		// How far above the placement's origin the disc is cast from, in world units. A placement
		// whose origin sits on the ground -- a tree, a rock -- casts from ground level, and since a
		// shadow only falls down, everything growing around that origin sits above the caster and
		// takes nothing while the ground between takes it in full; every such edge then flickers
		// under the temporal jitter. Lifting the cast point above the surrounding clutter puts all
		// of it below the caster, where the disc fades over it instead of cutting on and off.
		float casterLift = 0.0f;
	};
}
