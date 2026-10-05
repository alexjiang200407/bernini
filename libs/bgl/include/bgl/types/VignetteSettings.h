#pragma once

namespace bgl
{
	/** How a grade darkens a frame toward its corners. The default darkens nothing. */
	struct VignetteSettings
	{
		// How far a corner darkens: black at 1.
		float intensity = 0.0f;

		// How gradually it falls off from the centre, in (0, 1]; 0.2 is linear.
		float smoothness = 0.2f;
	};
}
