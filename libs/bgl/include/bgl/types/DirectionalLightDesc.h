#pragma once
#include <bgl/glm.h>

namespace bgl
{
	/**
	 * An analytic light: a sun, infinitely far away, casting no shadow. A view has one, which every
	 * shading model reads -- ISceneView::SetDirectionalLight says what it costs and what it refuses.
	 */
	struct DirectionalLightDesc
	{
		/// The direction the light *travels*, not the direction it is in. A midday sun is (0, -1, 0).
		glm::vec3 direction{ 0.0f, -1.0f, 0.0f };

		glm::vec3 color{ 1.0f };

		/// In the irradiance map's units rather than lux: a sun and an environment at the same number
		/// light a facing surface equally.
		float intensity = 0.0f;
	};
}
