#pragma once
#include <optional>

namespace bgl
{
	/**
	 * The shadow each foot of a hero skinned placement stands in: a capsule lying on the sole from
	 * the heel to the ball, cast straight down from the pose as drawn and fading with that foot's
	 * own height above each receiving pixel -- so a planted foot is dark and a lifted one fades.
	 * One per leg the rig's FootPlantDesc authored.
	 */
	struct FootShadowDesc
	{
		// The capsule's radius in world units, multiplied by the placement's uniform scale: half its
		// width, and how far it reaches past the heel and the ball.
		float radius = 0.1f;

		// Opacity where the sole touches the receiving surface, in [0, 1].
		float intensity = 0.75f;

		// Height of the sole above the receiving surface at which it has fully faded out. World
		// units.
		float fadeHeight = 0.3f;

		// How far a receiver may rise above the sole and still take the shadow, in world units. A
		// planted sole sits exactly on what it stands on, so at zero the depth buffer's own error
		// refuses half the ground under it, and the cobble tops around it are refused outright.
		// Unlike BlobShadowDesc::casterLift this moves only that cut-off, never where the fade is
		// measured from: a planted foot stays fully dark.
		float maxReceiverRise = 0.05f;
	};

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
		// Zero leaves only the feet.
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

		// A shadow under each foot as well as the disc. Only a placement ISceneView::HasFootIK holds
		// may carry one: a crowd instance has no pose of its own to find a foot in.
		std::optional<FootShadowDesc> feet;
	};
}
