#pragma once
#include <cstdint>
#include <utility>

namespace terrain
{
	/**
	 * Where woods grow: in hollows and on gentle, wet ground, never on a crest, past `maxSlope` or
	 * on water. Each sample scores a low-frequency noise `patchSize` metres across, plus its
	 * wetness and how far it lies in a hollow by their biases; the highest scorers become woods
	 * until they cover `coverage` of the field.
	 */
	struct ForestRule
	{
		float coverage    = 0.15f;   // of the field, in [0, 1]
		float patchSize   = 220.0f;  // metres across a patch of the noise
		float maxSlope    = 0.45f;   // rise over run
		float crestHeight = 1.5f;    // metres above its surroundings past which ground is a crest
		float wetnessBias = 0.6f;
		float hollowBias  = 0.5f;
		float minArea     = 3000.0f;  // square metres: a smaller wood is dropped
		float minClearing = 1500.0f;  // square metres: a smaller gap inside a wood is filled

		template <typename Self>
		Self&&
		SetCoverage(this Self&& self, float value) noexcept
		{
			self.coverage = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPatchSize(this Self&& self, float value) noexcept
		{
			self.patchSize = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMaxSlope(this Self&& self, float value) noexcept
		{
			self.maxSlope = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetCrestHeight(this Self&& self, float value) noexcept
		{
			self.crestHeight = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetWetnessBias(this Self&& self, float value) noexcept
		{
			self.wetnessBias = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetHollowBias(this Self&& self, float value) noexcept
		{
			self.hollowBias = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMinArea(this Self&& self, float value) noexcept
		{
			self.minArea = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMinClearing(this Self&& self, float value) noexcept
		{
			self.minClearing = value;
			return std::forward<Self>(self);
		}
	};

	/**
	 * Where rock breaks through: on steep ground and ridges, never in a wood or on water. Scored
	 * like a wood, by a noise plus how steep and how much of a ridge the ground is, over ground at
	 * least `minSlope` steep or standing above its surroundings.
	 */
	struct RockRule
	{
		float coverage  = 0.03f;
		float patchSize = 70.0f;
		float minSlope  = 0.3f;
		float slopeBias = 1.0f;
		float ridgeBias = 0.6f;
		float minArea   = 120.0f;

		template <typename Self>
		Self&&
		SetCoverage(this Self&& self, float value) noexcept
		{
			self.coverage = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPatchSize(this Self&& self, float value) noexcept
		{
			self.patchSize = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMinSlope(this Self&& self, float value) noexcept
		{
			self.minSlope = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSlopeBias(this Self&& self, float value) noexcept
		{
			self.slopeBias = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetRidgeBias(this Self&& self, float value) noexcept
		{
			self.ridgeBias = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMinArea(this Self&& self, float value) noexcept
		{
			self.minArea = value;
			return std::forward<Self>(self);
		}
	};

	/** Where water stands or runs: a lake at least `minLakeDepth` deep, or a channel `riverArea` drains through. */
	struct WaterRule
	{
		float minLakeDepth = 0.5f;    // metres
		float riverArea    = 2.0e5f;  // square metres

		template <typename Self>
		Self&&
		SetMinLakeDepth(this Self&& self, float value) noexcept
		{
			self.minLakeDepth = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetRiverArea(this Self&& self, float value) noexcept
		{
			self.riverArea = value;
			return std::forward<Self>(self);
		}
	};

	/**
	 * What GenerateMasks makes the masks of a field from. Hollow and ridge are measured as a
	 * sample's height against the mean within `positionRadius` metres (the topographic position
	 * index), read in units of `positionScale` metres: steadier at the size of a wood than the
	 * sample-scale curvature. A wood reads wetness averaged over the same radius.
	 */
	struct MaskDesc
	{
		uint32_t seed           = 1;
		float    positionRadius = 40.0f;
		float    positionScale  = 3.0f;

		ForestRule forest;
		RockRule   rock;
		WaterRule  water;

		template <typename Self>
		Self&&
		SetSeed(this Self&& self, uint32_t value) noexcept
		{
			self.seed = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPositionRadius(this Self&& self, float value) noexcept
		{
			self.positionRadius = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPositionScale(this Self&& self, float value) noexcept
		{
			self.positionScale = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetForest(this Self&& self, ForestRule value) noexcept
		{
			self.forest = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetRock(this Self&& self, RockRule value) noexcept
		{
			self.rock = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetWater(this Self&& self, WaterRule value) noexcept
		{
			self.water = value;
			return std::forward<Self>(self);
		}
	};
}
