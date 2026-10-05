#pragma once
#include <bgl/glm.h>

namespace bgl
{
	/**
	 * A colour split, under either post-process: red and blue displaced from green, as a
	 * misregistered print or a lens does it, on the scene ahead of the curve. The selection outline
	 * is not split. Distances are in pixels at a 2160-line output and scale with the output's
	 * height.
	 */
	struct ColorSplitSettings
	{
		// Red's displacement over the whole frame, +x right and +y down; blue takes the opposite.
		glm::vec2 offset{ -2.0f, 0.0f };

		// A lens's share: red's outward displacement at the middle of the top and bottom edges,
		// zero at the centre and linear in the distance from it. Negative pulls red inward.
		float radial = 0.0f;
	};
}
