#pragma once
#include <core/glm.h>
#include <cstdint>
#include <string>

namespace assetlib
{
	/** One blade's shape. Lengths are world units before the placement's scale. */
	struct GrassBladeParams
	{
		float    minHeight    = 0.3f;
		float    maxHeight    = 0.5f;
		float    rootWidth    = 0.03f;
		float    tipWidth     = 0.0f;  // a share of rootWidth
		float    curvature    = 0.3f;
		float    lean         = 0.2f;
		uint32_t nearSegments = 5;
		uint32_t farSegments  = 1;

		bool
		operator==(const GrassBladeParams&) const = default;
	};

	struct GrassClumpParams
	{
		uint32_t bladesPerClump = 8;
		float    radius         = 0.15f;

		bool
		operator==(const GrassClumpParams&) const = default;
	};

	struct GrassDensityParams
	{
		float fadeStart = 10.0f;
		float fadeEnd   = 60.0f;
		float widening  = 1.0f;

		bool
		operator==(const GrassDensityParams&) const = default;
	};

	struct GrassResponseParams
	{
		float stiffness    = 0.5f;
		float gustResponse = 1.0f;

		bool
		operator==(const GrassResponseParams&) const = default;
	};

	struct GrassLightingParams
	{
		float     rootOcclusion     = 0.6f;
		float     normalRounding    = 0.5f;
		float     groundNormalNear  = 0.0f;
		float     groundNormalFar   = 0.8f;
		glm::vec3 translucencyColor = glm::vec3(1.0f);
		float     translucency      = 0.0f;

		bool
		operator==(const GrassLightingParams&) const = default;
	};

	struct GrassColorParams
	{
		glm::vec3 rootTint  = glm::vec3(1.0f);
		glm::vec3 tipTint   = glm::vec3(1.0f);
		float     variation = 0.0f;

		bool
		operator==(const GrassColorParams&) const = default;
	};

	/**
	 * A `.bgrass`: a grass look, authored. The renderer's counterpart is `bgl::GrassDesc`, which
	 * documents what every value means and the range each must lie in; the defaults here are the
	 * same ones, so a document that omits a key draws exactly as one that spells out the default.
	 *
	 * Ranges are not checked on read. The renderer checks them where it creates the look, which is
	 * the one place they are stated.
	 */
	struct BGrass
	{
		std::string material;  // the `.bmaterial` its blades shade through; empty in a new document

		GrassBladeParams    blade;
		GrassClumpParams    clump;
		GrassDensityParams  density;
		GrassResponseParams response;
		GrassLightingParams lighting;
		GrassColorParams    color;

		// Every key this reader did not know, at any depth, written back as it was read.
		std::string extraJson = "{}";

		bool
		operator==(const BGrass&) const = default;
	};
}
