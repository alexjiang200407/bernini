#pragma once
#include <bgl/glm.h>

namespace bgl
{
	/**
	 * A vertical gradient across the frame, drawn behind the scene in place of the sky: `bottom` at
	 * the frame's bottom edge to `top` at its top, mixed linearly in screen height. It is fixed to the
	 * screen, so the camera moves nothing on it.
	 *
	 * Presentation, like SkyboxDesc::backdrop: both colours are scene-linear and drawn as they are,
	 * ahead of the display curve, without the view's exposure. The lighting is untouched.
	 *
	 * The default is the toon look-dev background -- pale horizon to sky blue -- which a toon look is
	 * judged against.
	 */
	struct BackdropGradient
	{
		glm::vec3 bottom{ 0.88f, 0.91f, 0.94f };
		glm::vec3 top{ 0.25f, 0.49f, 0.88f };

		[[nodiscard]] bool
		operator==(const BackdropGradient&) const noexcept = default;
	};
}
