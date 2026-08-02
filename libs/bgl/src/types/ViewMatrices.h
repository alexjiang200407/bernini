#pragma once

namespace bgl
{
	/**
	 * The camera matrices a view was drawn with, kept for one frame so the next can reproject
	 * through them.
	 */
	struct ViewMatrices
	{
		glm::mat4 viewProj{ 1.0f };

		// viewProj with the camera's translation dropped. The skybox sits at infinity, so a
		// translation moves it nowhere and only rotation displaces it on screen.
		glm::mat4 rotationOnlyViewProj{ 1.0f };

		// The sub-pixel offset in NDC that both matrices above were built with. Carried beside them
		// because a motion vector describes the surface, not the sample pattern, so the shader has
		// to subtract this frame's and last frame's before differencing.
		glm::vec2 jitter{ 0.0f };
	};
}
