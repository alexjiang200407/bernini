#pragma once
#include <bgl/glm.h>

namespace bgl
{
	/**
	 * The one analytic light: a sun, infinitely far away, casting no shadow.
	 *
	 * Additive on top of the view's environment map, which already integrates whatever sun its
	 * source HDR contained -- so an environment with a visible sun in it wants a dim analytic one or
	 * none at all. `intensity` defaults to 0 for that reason: a view that never sets a light renders
	 * exactly as it did before there were any. See docs/envmaps.md.
	 */
	struct DirectionalLightDesc
	{
		/**
		 * The direction the light *travels*, in world space. A midday sun is (0, -1, 0).
		 *
		 * Not the direction the sun is in, which is the other convention and inverts every scene
		 * silently if the two are mixed.
		 *
		 * Need not be normalized; ISceneView::SetDirectionalLight normalizes it and rejects a
		 * zero-length one, since a sun with no direction has no meaning to fall back on.
		 */
		glm::vec3 direction{ 0.0f, -1.0f, 0.0f };

		/// The light's colour. Its scale is `intensity`'s job, so this is a hue, not a radiance.
		glm::vec3 color{ 1.0f };

		/**
		 * The light's strength, in the units the environment's irradiance map holds -- the value
		 * that map would carry if this sun were the whole environment. So a sun and an IBL at the
		 * same number light a matte surface facing them equally, and a number read off
		 * scripts/blender_probe.py's `irradiance` is directly usable here.
		 *
		 * Not lux: bernini's exposure is a bare linear multiplier with no aperture or shutter behind
		 * it, so photometric units would be notation over an arbitrary scale.
		 */
		float intensity = 0.0f;
	};
}
