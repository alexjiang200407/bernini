#pragma once
#include <bgl/glm.h>

namespace bgl
{
	/**
	 * The one analytic light: a sun, infinitely far away, casting no shadow.
	 *
	 * Additive on the view's environment map, which already integrates whatever sun its source HDR
	 * held. ISceneView::SetDirectionalLight is what that costs and what it refuses.
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
