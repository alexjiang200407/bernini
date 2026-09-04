#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace bgl
{
	/**
	 * A weight moving from `from` to `to` over `[start, end)` in RenderJob::time: `from` before
	 * `start`, `to` from `end`, linear between. An end equal to its start is a step at the start;
	 * ISceneView::SetFootIK refuses one before it. The default is weight one, held forever.
	 *
	 * The record holds what to evaluate and never the evaluated value, so the pose at any time is
	 * a function of the record and the clock alone -- which is what lets a write leave the pose
	 * the previous frame drew unchanged, provided it starts at or after now.
	 */
	struct WeightRamp
	{
		float from  = 1.0f;
		float to    = 1.0f;
		float start = 0.0f;
		float end   = 0.0f;

		[[nodiscard]] static WeightRamp
		Constant(float value) noexcept
		{
			return { value, value, 0.0f, 0.0f };
		}

		/** What the pose pass reads at `time`. */
		[[nodiscard]] float
		At(float time) const noexcept
		{
			if (time < start)
			{
				return from;
			}
			if (time >= end)
			{
				return to;
			}
			return from + (to - from) * ((time - start) / (end - start));
		}

		/** A ramp from what this one holds at `now` to `target`, over `duration` seconds. */
		[[nodiscard]] WeightRamp
		FadeTo(float target, float now, float duration) const noexcept
		{
			return { At(now), target, now, now + duration };
		}
	};

	/**
	 * One leg's runtime IK weights, each multiplying the weight the cook baked per frame: how far
	 * the solve carries the ankle onto the ground, and how far the sole turns onto the ground's
	 * slope. Unity's `SetIKPositionWeight` and `SetIKRotationWeight`, per foot.
	 */
	struct FootIKLegDesc
	{
		WeightRamp position;
		WeightRamp rotation;
	};

	/** Legs a rig may carry; a FootIKDesc has a slot for each. */
	inline constexpr uint32_t c_MaxLegsPerRig = 8;

	/**
	 * A hero skinned instance's runtime foot-IK weights, one entry per leg in the order the rig's
	 * FootPlantDesc listed them. Written whole by ISceneView::SetFootIK, on an event and never per
	 * frame. Entries past the rig's leg count are not stored, and read back as the default.
	 *
	 * Every weight is one until written, so an instance nobody writes plants exactly as the baked
	 * weights say.
	 */
	struct FootIKDesc
	{
		std::array<FootIKLegDesc, c_MaxLegsPerRig> leg = {};

		/** Every leg held at `position` and `rotation`. */
		[[nodiscard]] static FootIKDesc
		Constant(float position, float rotation) noexcept
		{
			auto desc = FootIKDesc();
			for (FootIKLegDesc& entry : desc.leg)
			{
				entry.position = WeightRamp::Constant(position);
				entry.rotation = WeightRamp::Constant(rotation);
			}
			return desc;
		}

		/**
		 * Every leg fading from what `current` holds at `now` to `position` and `rotation` over
		 * `duration` seconds. Starts at `now`, so a write built from the record the instance holds
		 * leaves everything before `now` as it was.
		 */
		[[nodiscard]] static FootIKDesc
		FadeTo(
			const FootIKDesc& current,
			float             now,
			float             duration,
			float             position,
			float             rotation) noexcept
		{
			auto desc = FootIKDesc();
			for (size_t i = 0; i < desc.leg.size(); ++i)
			{
				desc.leg[i].position = current.leg[i].position.FadeTo(position, now, duration);
				desc.leg[i].rotation = current.leg[i].rotation.FadeTo(rotation, now, duration);
			}
			return desc;
		}
	};
}
