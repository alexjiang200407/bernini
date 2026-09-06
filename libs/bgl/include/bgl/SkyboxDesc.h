#pragma once
#include <bgl/TextureAssetHandle.h>
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

		// Radians about the up axis. Rotates the lighting with it: the IBL lookup carries the same
		// spin, or a rotated sky would light the scene from where it used to be.
		float rotationY = 0.0f;
	};
}