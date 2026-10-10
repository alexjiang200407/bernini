#pragma once
#include <assetlib_structs/Heightfield.h>
#include <cstdint>
#include <terrainlib/TerrainFields.h>
#include <terrainlib/TerrainLayer.h>
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
	/**
	 * Where each kind of thing stands on a field, one layer per kind laid as its heightfield is, so
	 * a channel painted by hand later replaces one kind without touching the others. A mask is hard:
	 * every value is 0 or 1, and every region in it is at least the area its rule asked for.
	 */
	struct TerrainMasks
	{
		// 1 inside a wood.
		TerrainLayer forest;

		// Inside a wood, the distance in metres from the sample to the nearest sample outside it; 0
		// outside. What sizes a tree by how deep in its wood it stands.
		TerrainLayer forestDepth;

		// 1 where rock breaks through the ground.
		TerrainLayer rock;

		// 1 where water stands or runs: a lake deep enough, or a channel enough ground drains
		// through. Nothing else of the masks lies on it.
		TerrainLayer water;
	};

	/**
	 * The masks of `field`, whose fields are `fields`, by `desc`'s rules. Water first, from the
	 * fields alone; then woods off the water, cleaned -- woods under the rule's minimum area
	 * dropped, clearings under its minimum filled -- and the forest's depth by an exact Euclidean
	 * distance transform (Felzenszwalb and Huttenlocher 2012); then rock off both, under its
	 * minimum area dropped. A wood or an outcrop is a group of samples joined along either axis or
	 * diagonally. A mask painted by hand later takes the noise's place and keeps the cleanup.
	 *
	 * Deterministic from `desc.seed`; linear in the samples but for a selection per kind.
	 *
	 * @pre `fields` is DeriveFields(field).
	 * @throws std::runtime_error naming the first field of `desc` outside the range its comment
	 *         gives: a coverage outside [0, 1], or a size, area, scale or slope not finite and
	 *         positive.
	 */
	[[nodiscard]] TerrainMasks
	GenerateMasks(
		const assetlib::Heightfield& field,
		const TerrainFields&         fields,
		const MaskDesc&              desc);
}
