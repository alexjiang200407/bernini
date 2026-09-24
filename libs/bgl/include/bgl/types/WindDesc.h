#pragma once
#include <bgl/glm.h>

namespace bgl
{
	/**
	 * A view's wind: a steady push along `direction` plus a gust field scrolling with it. What moves
	 * is up to each grass look's response (GrassResponseDesc). The default is calm.
	 */
	struct WindDesc
	{
		// Need not be unit length; only its horizontal part is used.
		glm::vec3 direction = glm::vec3(1.0f, 0.0f, 0.0f);

		// The steady push, in [0, 1] of a blade's full bend.
		float strength = 0.0f;

		// The gusts: their size in world units, the speed they travel at in world units per second,
		// and how much they add to `strength` at their peak.
		float gustScale    = 8.0f;
		float gustSpeed    = 2.0f;
		float gustStrength = 0.0f;
	};
}
