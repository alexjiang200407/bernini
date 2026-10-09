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
	 * height rule and a patch rule, each a share in [0, 1], and of the ground's own cover where the
	 * layer follows it. A clump scaled to nothing is not drawn, and one near a rule's edge grows
	 * shorter, so grass thins out toward rock and snow rather than ending on a line.
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

		// How wide a patch's edge is, as a share of the patch noise either side of the coverage
		// threshold, in (0, 0.5]: across it a clump grows from nothing to full height, so a wider
		// edge lets a field shorten into a bare patch rather than end on a line.
		float patchEdge = 0.06f;

		// Each clump also grows by the ground's cover under it, as the terrain's surface says it
		// (PbrSurface::groundCover, read off the view's Ground Color pass, which the layer then
		// makes the view draw). A surface that says nothing covers everything. Off by default.
		bool groundCover = false;

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

		template <typename Self>
		Self&&
		SetPatchEdge(this Self&& self, float value) noexcept
		{
			self.patchEdge = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetGroundCover(this Self&& self, bool value) noexcept
		{
			self.groundCover = value;
			return std::forward<Self>(self);
		}
	};
}
