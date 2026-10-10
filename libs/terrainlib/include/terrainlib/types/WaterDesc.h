#pragma once
#include <cstdint>
#include <utility>

namespace terrain
{
	/**
	 * Where rivers run and how they are cut: a river begins where `area` square metres drain
	 * through the ground, and widens with the square root of what drains through it, `width` metres
	 * wide where a square kilometre does, between `minWidth` and `maxWidth`. A course is smoothed
	 * over `smoothing` metres; a tributary shorter than `minLength` is dropped. An `area` of 0 runs
	 * no river.
	 */
	struct RiverRule
	{
		float area      = 3.0e5f;  // square metres
		float width     = 14.0f;   // metres, where a square kilometre drains through
		float minWidth  = 5.0f;    // metres
		float maxWidth  = 28.0f;   // metres
		float depth     = 0.2f;    // metres of water at the middle, per metre of width
		float minDepth  = 1.2f;    // metres
		float maxDepth  = 3.0f;    // metres
		float bank      = 8.0f;    // metres a bank takes to rise back to the ground beside it
		float smoothing = 24.0f;   // metres
		float minLength = 120.0f;  // metres

		// Metres a second the water runs on level ground, and how much faster per unit of fall
		// (rise over run), up to `maxSpeed`.
		float speed      = 0.5f;
		float speedSlope = 20.0f;
		float maxSpeed   = 2.5f;

		template <typename Self>
		Self&&
		SetArea(this Self&& self, float value) noexcept
		{
			self.area = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetWidth(this Self&& self, float value) noexcept
		{
			self.width = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMinWidth(this Self&& self, float value) noexcept
		{
			self.minWidth = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMaxWidth(this Self&& self, float value) noexcept
		{
			self.maxWidth = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDepth(this Self&& self, float value) noexcept
		{
			self.depth = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMinDepth(this Self&& self, float value) noexcept
		{
			self.minDepth = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMaxDepth(this Self&& self, float value) noexcept
		{
			self.maxDepth = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetBank(this Self&& self, float value) noexcept
		{
			self.bank = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSmoothing(this Self&& self, float value) noexcept
		{
			self.smoothing = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMinLength(this Self&& self, float value) noexcept
		{
			self.minLength = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSpeed(this Self&& self, float value) noexcept
		{
			self.speed = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSpeedSlope(this Self&& self, float value) noexcept
		{
			self.speedSlope = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMaxSpeed(this Self&& self, float value) noexcept
		{
			self.maxSpeed = value;
			return std::forward<Self>(self);
		}
	};

	/**
	 * Where lakes lie and how they are dug: up to `count` of them, on the flattest low ground that
	 * gathers water, at least `spacing` metres apart, each `radius` metres across at most, its
	 * shore pushed in and out by up to `shoreNoise` of the radius. A lake's level is the lowest
	 * point of its rim, where it spills; it is dug `depth` metres below that at its middle, and its
	 * banks rise back to the ground over `bank` metres. A `count` of 0 digs none.
	 */
	struct LakeRule
	{
		uint32_t count      = 3;
		float    minRadius  = 45.0f;   // metres
		float    maxRadius  = 90.0f;   // metres
		float    depth      = 4.0f;    // metres
		float    maxSlope   = 0.12f;   // rise over run, averaged over the lake
		float    spacing    = 400.0f;  // metres between two lakes' middles
		float    bank       = 12.0f;   // metres
		float    shoreNoise = 0.3f;    // of the radius, in [0, 1)

		template <typename Self>
		Self&&
		SetCount(this Self&& self, uint32_t value) noexcept
		{
			self.count = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetRadius(this Self&& self, float min, float max) noexcept
		{
			self.minRadius = min;
			self.maxRadius = max;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDepth(this Self&& self, float value) noexcept
		{
			self.depth = value;
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
		SetSpacing(this Self&& self, float value) noexcept
		{
			self.spacing = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetBank(this Self&& self, float value) noexcept
		{
			self.bank = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetShoreNoise(this Self&& self, float value) noexcept
		{
			self.shoreNoise = value;
			return std::forward<Self>(self);
		}
	};

	/** What CarveWater cuts into a field: its rivers and its lakes, deterministic from `seed`. */
	struct WaterDesc
	{
		uint32_t  seed = 1;
		RiverRule rivers;
		LakeRule  lakes;

		template <typename Self>
		Self&&
		SetSeed(this Self&& self, uint32_t value) noexcept
		{
			self.seed = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetRivers(this Self&& self, RiverRule value) noexcept
		{
			self.rivers = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetLakes(this Self&& self, LakeRule value) noexcept
		{
			self.lakes = value;
			return std::forward<Self>(self);
		}
	};
}
