#pragma once

namespace bgl
{
	/** How a target blooms: added to the scene in linear radiance, ahead of the display curve. */
	struct BloomSettings
	{
		// The glow's weight.
		float intensity = 0.25f;

		// Linear radiance after exposure, which puts a scene's average near 0.18. Zero blooms the
		// whole frame, a diffusion filter.
		float threshold = 0.5f;

		// The threshold's fade-in, as a share of it: 0 is a hard cut.
		float softKnee = 0.5f;

		// How far the glow spreads: the coarser level's weight at each upsample.
		float scatter = 0.7f;
	};
}
