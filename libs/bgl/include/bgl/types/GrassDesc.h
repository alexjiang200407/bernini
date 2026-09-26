#pragma once
#include <bgl/MaterialHandle.h>
#include <bgl/glm.h>
#include <cstdint>

namespace bgl
{
	/** Segments a blade may have along its length, near the camera. */
	constexpr uint32_t c_MaxGrassBladeSegments = 7;

	/** Blades one clump may grow. */
	constexpr uint32_t c_MaxGrassBladesPerClump = 16;

	/**
	 * One blade: a tapered strip bent along a quadratic Bezier from the root to the tip. Lengths
	 * are world units before the placement's scale.
	 */
	struct GrassBladeDesc
	{
		// Each blade's height is drawn between these, then multiplied by its clump's height scale.
		float minHeight = 0.3f;
		float maxHeight = 0.5f;

		float rootWidth = 0.03f;

		// The tip's width as a share of the root's, in [0, 1]. Zero comes to a point.
		float tipWidth = 0.0f;

		// How the blade bends on its way to a leaning tip, in [0, 1]: zero runs straight from root to
		// tip, one stands upright at the root and arcs over near the tip.
		float curvature = 0.3f;

		// How far the tip leans out from above its root at rest, in [0, 1] of its height. The blade
		// keeps its length, so a leaning tip also sits lower.
		float lean = 0.2f;

		// Segments along the blade at the camera and at the fade end, interpolated between.
		// 1 <= farSegments <= nearSegments <= c_MaxGrassBladeSegments.
		uint32_t nearSegments = 5;
		uint32_t farSegments  = 1;
	};

	struct GrassClumpDesc
	{
		// In [1, c_MaxGrassBladesPerClump].
		uint32_t bladesPerClump = 8;

		// How far from its clump's point a blade's root may land, in world units.
		float radius = 0.15f;
	};

	/**
	 * How the field thins with distance from the camera: every blade is kept up to `fadeStart`,
	 * none past `fadeEnd`, and the kept share falls linearly between. A blade is kept by comparing
	 * a hash of its index against that share, so blades vanish one at a time rather than in bands.
	 */
	struct GrassDensityDesc
	{
		float fadeStart = 10.0f;
		float fadeEnd   = 60.0f;

		// A surviving blade's width is multiplied by 1 + widening * (1 - kept share), so the field's
		// coverage holds as it thins. Zero keeps every blade its authored width.
		float widening = 1.0f;
	};

	/**
	 * How a blade answers what bends it: the view's wind (ISceneView::SetWind). Every force bends
	 * a blade about its root without stretching it, so no force moves it out of its chunk's bound.
	 */
	struct GrassResponseDesc
	{
		// In [0, 1]: at 1 the blade stands still whatever pushes it.
		float stiffness = 0.5f;

		// Multiplies the gust field's contribution; zero sways with the steady wind alone.
		float gustResponse = 1.0f;
	};

	/**
	 * The terms that are the blade's geometry rather than its surface. Every share is in [0, 1].
	 */
	struct GrassLightingDesc
	{
		// How much darker the root is than the tip; multiplies the material's occlusion.
		float rootOcclusion = 0.6f;

		// How far the normal rounds across the blade's width, away from the flat face.
		float normalRounding = 0.5f;

		// How far the shading normal blends toward the clump's ground normal, at the camera and at
		// the fade end. 1 and 1 shade every blade with the ground's normal.
		float groundNormalNear = 0.0f;
		float groundNormalFar  = 0.8f;

		// The sun through a blade seen against it, added after the material is lit. Ignored by a
		// material whose surface owns its lighting. Zero adds nothing; the colour is linear.
		glm::vec3 translucencyColor = glm::vec3(1.0f);
		float     translucency      = 0.0f;
	};

	/** Linear multipliers on the material's base colour. White everywhere leaves it as it is. */
	struct GrassColorDesc
	{
		glm::vec3 rootTint = glm::vec3(1.0f);
		glm::vec3 tipTint  = glm::vec3(1.0f);

		// In [0, 1]: how far each blade's brightness varies by its own random.
		float variation = 0.0f;
	};

	/**
	 * A grass look: what IScene::CreateGrass takes and what a `.bgrass` document holds. Placement is
	 * not part of it -- the clumps come with the geom the look is bound to.
	 */
	struct GrassDesc
	{
		// Drawn opaque whatever its layer: a blade is solid geometry, so a mask or hashed material's
		// alpha is never tested. A blended material is refused.
		MaterialHandle material;

		GrassBladeDesc    blade;
		GrassClumpDesc    clump;
		GrassDensityDesc  density;
		GrassResponseDesc response;
		GrassLightingDesc lighting;
		GrassColorDesc    color;
	};
}
