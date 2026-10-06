#pragma once
#include <bgl/types/GrassHandle.h>
#include <cstdint>
#include <limits>
#include <numbers>
#include <utility>

namespace bgl
{
	/**
	 * Tiles a terrain layer's window may span along each side: its look's fade end over its tiles
	 * of eight clumps, `spacing` apart. One amplification group runs per tile every frame.
	 */
	constexpr uint32_t c_MaxTerrainGrassWindowTiles = 255;

	/**
	 * One layer of grass on a terrain -- see IScene::AttachTerrainGrass. Clumps of `look` stand
	 * `spacing` apart over the whole field, each jittered within its cell and placed on the
	 * heightfield, and the rules below decide how tall each grows: the product of a slope rule, a
	 * height rule and a patch rule, each a share in [0, 1]. A clump scaled to nothing is not drawn,
	 * and one near a rule's edge grows shorter, so grass thins out toward rock and snow rather
	 * than ending on a line.
	 */
	struct TerrainGrassDesc
	{
		GrassHandle look;

		// World units between neighbouring clumps; each grows the look's bladesPerClump blades. A
		// spacing so fine against the look's fade end that the window would pass
		// c_MaxTerrainGrassWindowTiles is refused.
		float spacing = 0.25f;

		// The steepest ground, in radians from level, grass stands full height on, in [0, pi/2],
		// and over how many radians steeper it shrinks to nothing, non-negative and ending at
		// vertical if it would pass it. The default grows on any slope.
		float maxSlope   = std::numbers::pi_v<float> * 0.5f;
		float slopeBlend = 0.0f;

		// The world heights grass grows between, and how far inside either bound it grows from
		// nothing to full height; zero is a hard edge. The default is unbounded.
		float minHeight   = std::numeric_limits<float>::lowest();
		float maxHeight   = std::numeric_limits<float>::max();
		float heightBlend = 0.0f;

		// Grass grows in patches of about `patchSize` world units across a low-frequency noise,
		// covering about `patchCoverage` of the ground the other rules allow, in [0, 1]. One is
		// unbroken ground cover and zero is none.
		float patchSize     = 20.0f;
		float patchCoverage = 1.0f;

		template <typename Self>
		Self&&
		SetLook(this Self&& self, GrassHandle value) noexcept
		{
			self.look = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSpacing(this Self&& self, float value) noexcept
		{
			self.spacing = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSlope(this Self&& self, float maxSlope, float blend) noexcept
		{
			self.maxSlope   = maxSlope;
			self.slopeBlend = blend;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetHeights(this Self&& self, float minHeight, float maxHeight, float blend) noexcept
		{
			self.minHeight   = minHeight;
			self.maxHeight   = maxHeight;
			self.heightBlend = blend;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPatches(this Self&& self, float size, float coverage) noexcept
		{
			self.patchSize     = size;
			self.patchCoverage = coverage;
			return std::forward<Self>(self);
		}
	};
}
