#pragma once
#include <bgl/glm.h>
#include <bgl/types/VignetteSettings.h>

namespace bgl
{
	/**
	 * PostProcess's grade, ahead of either display curve: white balance and the vignette in scene
	 * linear, the ASC CDL and contrast in the log coordinate AgX's formation reads, decoded back
	 * to scene linear for Gran Turismo's. Every default is neutral. See docs/passes.md § The colour
	 * grade.
	 */
	struct ColorGradeSettings
	{
		// Within [-100, 100]. Positive is warmer, and positive tint is more magenta than green.
		float temperature = 0.0f;
		float tint        = 0.0f;

		// (x * slope + offset) ^ power on AgX's 25-stop log encoding, then saturation about Rec.709
		// luma.
		glm::vec3 slope{ 1.0f };
		glm::vec3 offset{ 0.0f };
		glm::vec3 power{ 1.0f };
		float     saturation = 1.0f;

		// About middle grey in the same encoding, so 0.18 stays where the curve put it.
		float contrast = 1.0f;

		VignetteSettings vignette;
	};
}
