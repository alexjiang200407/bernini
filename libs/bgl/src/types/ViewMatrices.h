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
	};
}
