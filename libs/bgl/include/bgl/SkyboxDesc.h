#pragma once
#include <bgl/TextureAssetHandle.h>
#include <bgl/glm.h>
#include <cstdint>

namespace bgl
{
	struct SkyboxDesc
	{
		TextureAssetHandle skyboxCubeTex;

		// Which mip the backdrop samples. Above 0 defocuses it, reading as depth of field -- and
		// only as far as the cube has levels, so a single-mip sky ignores this rather than failing.
		uint32_t mipLevel = 0;

		/**
		 * An *additional* gain on top of ISceneView::SetExposure, not a replacement for it.
		 *
		 * The view's exposure is a property of the environment's maps and applies to everything lit
		 * by them, the backdrop included; this is the per-sky trim on top. So 1.0 means "no extra
		 * gain" -- it does not mean the sky ignores the environment, which is what it used to mean
		 * and is why a backdrop sat a stop away from the objects in front of it.
		 */
		float exposure = 1.0f;

		// Radians about the up axis -- the world's, or the camera's when followsView. Rotates the
		// lighting with it: the IBL lookup carries the same spin, or a rotated sky would light the
		// scene from where it used to be.
		float rotationY = 0.0f;

		// Attaches the environment to the camera instead of the world, lighting and backdrop
		// alike, so its light arrives from the same screen direction however the camera orbits.
		// See docs/envmaps.md.
		bool followsView = false;

		/**
		 * How much of the backdrop is the sky, the rest being `backdrop`: 1 draws the sky alone.
		 * The lighting is untouched either way -- this is presentation, applied before the display
		 * curve, so `backdrop` is scene-linear.
		 */
		float     opacity = 1.0f;
		glm::vec3 backdrop{ 0.0f };
	};
}