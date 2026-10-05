#pragma once
#include <cstdint>

namespace bgl
{
	/**
	 * Film grain, under either post-process: monochrome noise scaled by the pixel's displayed value,
	 * after the curve and the grade, so black stays black.
	 */
	struct FilmGrainSettings
	{
		// The largest share of its display-linear value a pixel moves by, in [0, 1]. The display
		// clamps at white, so a pixel above 1 / (1 + intensity) loses the top of its upward swing.
		float intensity = 0.12f;

		// The grain's pitch in pixels at a 2160-line output, scaled with the output's height and
		// floored at one output pixel.
		float size = 2.0f;

		// How many of the target's frames one pattern is held for; zero never changes it.
		uint32_t holdFrames = 1;
	};
}
