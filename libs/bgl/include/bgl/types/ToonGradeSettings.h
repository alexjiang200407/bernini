#pragma once
#include <bgl/glm.h>
#include <bgl/types/VignetteSettings.h>

namespace bgl
{
	/**
	 * ToonPostProcess's grade, on the value the display shows: white balance and the vignette in
	 * display linear, then the levels on the sRGB-encoded value -- its [0, 1] mapped onto [black,
	 * white] through gamma -- then saturation and contrast. Every default is neutral. See
	 * docs/passes.md § The colour grade.
	 */
	struct ToonGradeSettings
	{
		// Within [-100, 100]. Positive is warmer, and positive tint is more magenta than green.
		float temperature = 0.0f;
		float tint        = 0.0f;

		// The black and the white the display shows, per channel, sRGB-encoded, so a film print's
		// lifted, tinted black and its cream white are read straight off a frame. Each in [0, 1].
		glm::vec3 black{ 0.0f };
		glm::vec3 white{ 1.0f };

		// Above one darkens the middle, holding both ends.
		glm::vec3 gamma{ 1.0f };

		// About Rec.709 luma.
		float saturation = 1.0f;

		// About middle grey's encoding, so 0.18 stays where it was.
		float contrast = 1.0f;

		VignetteSettings vignette;
	};
}
