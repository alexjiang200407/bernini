#pragma once
#include <utility>

namespace terrain
{
	/**
	 * How a generated field is stepped into strata before it is eroded: each step of
	 * `stepHeight` metres a near-level shelf that climbs `shelfRise` of the step, then a face that
	 * climbs the rest -- from a scree's angle at its foot to steep, rounding over at its lip -- as
	 * bedded rock weathers into benches and cliffs. The steps are cut into the land smoothed over
	 * `smoothing` metres and `detail` of what that smoothed away laid back over them, so a face
	 * follows the land's broad contour rather than every bump of it; minor steps `minorStep` of a step tall,
	 * `minorStrength` as deep, break the faces and shelves up. The strata dip
	 * by `tilt` and their edges wander by `edgeNoise` metres over `noiseWavelength`, so the steps
	 * never read as contour lines; each step's height varies by `jitter` of itself across the
	 * field. Only ground more than `startHeight` metres above the field's lowest is stepped,
	 * fading in over `fadeHeight`, so valleys and meadows keep the noise's own slopes. Every length
	 * is in world units. The default steps nothing.
	 */
	struct TerraceDesc
	{
		// The metres a step climbs; 0 terraces nothing.
		float stepHeight = 0.0f;

		// The share of a step's horizontal run, in (0, 1), that is shelf rather than face.
		float shelf = 0.6f;

		// The share of a step's height, in [0, 1), the shelf climbs: 0 is a dead-level bench.
		float shelfRise = 0.15f;

		// The share of `stepHeight`, in [0, 1), a step's height varies by across the field.
		float jitter = 0.3f;

		// The metres the land is smoothed over before it is stepped, 0 stepping every bump, and the
		// share, in [0, 1], of the detail smoothed away laid back over the steps.
		float smoothing = 24.0f;
		float detail    = 0.3f;

		// The share of a step, in [0, 1), a minor step climbs, 0 for none, and how deep it is cut,
		// in [0, 1].
		float minorStep     = 0.25f;
		float minorStrength = 0.5f;

		// The strata's dip, rise over run, along a direction fixed by the seed.
		float tilt = 0.05f;

		// The metres a step's edge wanders up or down, and the metres its wandering repeats over.
		float edgeNoise       = 10.0f;
		float noiseWavelength = 150.0f;

		// The metres above the field's lowest ground stepping starts at, and the metres it fades in over.
		float startHeight = 40.0f;
		float fadeHeight  = 60.0f;

		[[nodiscard]] bool
		Terraces() const noexcept
		{
			return stepHeight > 0.0f;
		}

		template <typename Self>
		Self&&
		SetStepHeight(this Self&& self, float value) noexcept
		{
			self.stepHeight = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetShelf(this Self&& self, float share, float rise) noexcept
		{
			self.shelf     = share;
			self.shelfRise = rise;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSmoothing(this Self&& self, float metres, float detail) noexcept
		{
			self.smoothing = metres;
			self.detail    = detail;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMinorSteps(this Self&& self, float share, float strength) noexcept
		{
			self.minorStep     = share;
			self.minorStrength = strength;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetJitter(this Self&& self, float value) noexcept
		{
			self.jitter = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetTilt(this Self&& self, float value) noexcept
		{
			self.tilt = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetEdgeNoise(this Self&& self, float metres, float wavelength) noexcept
		{
			self.edgeNoise       = metres;
			self.noiseWavelength = wavelength;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetHeights(this Self&& self, float start, float fade) noexcept
		{
			self.startHeight = start;
			self.fadeHeight  = fade;
			return std::forward<Self>(self);
		}
	};
}
