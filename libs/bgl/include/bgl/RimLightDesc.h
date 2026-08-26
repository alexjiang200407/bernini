#pragma once
#include <bgl/glm.h>

namespace bgl
{
	/**
	 * The view's rim light: the environment sampled away from the camera, weighted towards a
	 * surface's silhouette.
	 *
	 * A readability device rather than a light in the shading model's sense -- it ignores albedo, so
	 * a dark surface separates from a bright sky as well as a pale one does. What each instance
	 * catches of it is that instance's own; see ISceneView::SetInstanceRimIntensity.
	 */
	struct RimLightDesc
	{
		glm::vec3 tint = glm::vec3(1.0f);

		// Zero is a rim light that is off, which is what a view that was never given one has.
		float intensity = 0.0f;

		// Falloff towards the silhouette; higher is a narrower band.
		float power = 4.0f;
	};
}
