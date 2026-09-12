#include <algorithm>
#include <bgl/InstanceDesc.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gamelib/BlendSpaceInfo.h>
#include <gamelib/ClipInfo.h>
#include <gamelib/anim_blend.h>
#include <span>

#include <core/err/util.h>

namespace game
{
	using core::throw_runtime_error_if;

	namespace
	{
		/** The window a write opens: `[now, now + duration]`, refused if it runs backwards. */
		void
		RequireWindow(float now, float duration, const char* what)
		{
			throw_runtime_error_if(
				!std::isfinite(now) || !std::isfinite(duration),
				"{}: a window of [{}, {}] is not finite",
				what,
				now,
				duration);
			throw_runtime_error_if(
				duration < 0.0f,
				"{}: a fade cannot take {} seconds; a write only changes the future",
				what,
				duration);
		}

		/**
		 * How much normalized phase elapses over `seconds` while the cycle length moves linearly
		 * from `from` to `to`. The closed form of the integral the pose pass evaluates, and the
		 * same limit when the cycle is not moving.
		 */
		float
		PhaseElapsed(float seconds, float from, float to)
		{
			const float delta = to - from;
			if (std::abs(delta) < 1e-6f * std::max(from, 1.0f))
				return seconds / from;

			return seconds * std::log(to / from) / delta;
		}

		/**
		 * The space phase `slot` has reached at `now`, integrated over the parameter path it is
		 * already on. Split at the ramp's ends and at each sample the parameter crosses, because
		 * the cycle length is only linear between two adjacent samples.
		 */
		float
		SpacePhaseAt(
			const bgl::PlaybackSlot&  slot,
			const BlendSpaceInfo&     space,
			std::span<const ClipInfo> clips,
			float                     now)
		{
			if (now <= slot.tRef)
				return slot.phase;

			float elapsed = 0.0f;

			const float rampFrom = std::clamp(slot.paramStart, slot.tRef, now);
			if (rampFrom > slot.tRef)
				elapsed += (rampFrom - slot.tRef) / space.SecondsAt(clips, slot.param0);

			const float rampTo = std::clamp(slot.paramEnd, rampFrom, now);
			if (rampTo > rampFrom)
			{
				const float pFrom = SlotParameterAt(slot, rampFrom);
				const float pTo   = SlotParameterAt(slot, rampTo);

				float tSegment = rampFrom;
				float pSegment = pFrom;
				float dSegment = space.SecondsAt(clips, pFrom);

				// In the order the ramp reaches them, not in table order: a falling parameter
				// crosses the samples backwards, and a segment accumulated out of order fuses two
				// spans of a kinked cycle into one.
				const bool ascending = pTo > pFrom;
				for (size_t k = 0; k < space.samples.size(); ++k)
				{
					const size_t i    = ascending ? k : space.samples.size() - 1 - k;
					const float  edge = space.samples[i].parameter;

					if (!(ascending ? (edge > pSegment && edge < pTo) :
					                  (edge < pSegment && edge > pTo)))
						continue;

					const float tEdge = std::lerp(rampFrom, rampTo, (edge - pFrom) / (pTo - pFrom));
					const float dEdge = space.SecondsAt(clips, edge);

					elapsed += PhaseElapsed(tEdge - tSegment, dSegment, dEdge);

					tSegment = tEdge;
					pSegment = edge;
					dSegment = dEdge;
				}

				elapsed += PhaseElapsed(rampTo - tSegment, dSegment, space.SecondsAt(clips, pTo));
			}

			if (now > rampTo)
				elapsed += (now - rampTo) / space.SecondsAt(clips, slot.param1);

			const float phase = slot.phase + slot.rate * elapsed;
			return phase - std::floor(phase);
		}
	}

	float
	SlotWeightAt(const bgl::PlaybackSlot& slot, float time) noexcept
	{
		if (time < slot.rampStart)
			return slot.weight0;
		if (time >= slot.rampEnd)
			return slot.weight1;

		return std::lerp(
			slot.weight0,
			slot.weight1,
			(time - slot.rampStart) / (slot.rampEnd - slot.rampStart));
	}

	float
	SlotParameterAt(const bgl::PlaybackSlot& slot, float time) noexcept
	{
		if (time < slot.paramStart)
			return slot.param0;
		if (time >= slot.paramEnd)
			return slot.param1;

		return std::lerp(
			slot.param0,
			slot.param1,
			(time - slot.paramStart) / (slot.paramEnd - slot.paramStart));
	}

	bgl::SkinnedPlaybackDesc
	CrossfadeTo(
		const bgl::SkinnedPlaybackDesc& desc,
		uint32_t                        nodeIndex,
		float                           now,
		float                           duration,
		float                           phase,
		float                           rate,
		float                           parameter)
	{
		RequireWindow(now, duration, "CrossfadeTo");

		auto next = desc;

		// Already arriving at this node: restarting would mean the same request in two consecutive
		// frames never finishes, because each would begin the fade again from where it had got to.
		for (const bgl::PlaybackSlot& slot : next.slot)
			if (slot.nodeIndex == nodeIndex && slot.weight1 > 0.0f &&
			    SlotWeightAt(slot, now) < slot.weight1)
				return next;

		size_t incoming = next.slot.size();

		for (size_t s = 0; s < next.slot.size(); ++s)
		{
			bgl::PlaybackSlot& slot = next.slot[s];
			const float        held = SlotWeightAt(slot, now);

			// The one already playing `nodeIndex` becomes the incoming slot, so a fade back to what is
			// showing does not upload a second copy of it.
			if (slot.nodeIndex == nodeIndex && held > 0.0f)
			{
				incoming = s;
				continue;
			}

			// Ramped down, not dropped: a slot still weighted at prevTime is part of the pose the
			// frame before this one drew. Phase, rate and tRef are untouched, so what it plays is
			// unchanged at every clock -- only how much of it counts moves.
			slot.weight0   = held;
			slot.weight1   = 0.0f;
			slot.rampStart = now;
			slot.rampEnd   = now + duration;
		}

		if (incoming == next.slot.size())
		{
			incoming       = 0;
			float lightest = SlotWeightAt(next.slot[0], now);
			for (size_t s = 1; s < next.slot.size(); ++s)
			{
				const float held = SlotWeightAt(next.slot[s], now);
				if (held < lightest)
				{
					lightest = held;
					incoming = s;
				}
			}

			// A free slot is one at zero, so this picks one whenever there is one and only evicts
			// something audible when there is not.
			next.slot[incoming]           = bgl::PlaybackSlot();
			next.slot[incoming].nodeIndex = nodeIndex;
			next.slot[incoming].phase     = phase;
			next.slot[incoming].rate      = rate;
			next.slot[incoming].tRef      = now;

			// Both ends, so the slot holds one value rather than a ramp: a fade says where a space
			// arrives, and a parameter that moves afterwards is RetargetParameter's window.
			next.slot[incoming].param0 = parameter;
			next.slot[incoming].param1 = parameter;
		}

		bgl::PlaybackSlot& arriving = next.slot[incoming];
		arriving.weight0            = SlotWeightAt(arriving, now);
		arriving.weight1            = 1.0f;
		arriving.rampStart          = now;
		arriving.rampEnd            = now + duration;

		return next;
	}

	bgl::SkinnedPlaybackDesc
	RetargetParameter(
		const bgl::SkinnedPlaybackDesc& desc,
		uint32_t                        nodeIndex,
		const BlendSpaceInfo&           space,
		std::span<const ClipInfo>       clips,
		float                           parameter,
		float                           now,
		float                           duration)
	{
		RequireWindow(now, duration, "RetargetParameter");

		for (const BlendSpaceSampleInfo& sample : space.samples)
		{
			throw_runtime_error_if(
				sample.clipIndex >= clips.size(),
				"RetargetParameter: the space '{}' names clip {} of an acquire that holds {}",
				space.name,
				sample.clipIndex,
				clips.size());
		}

		auto next = desc;

		for (bgl::PlaybackSlot& slot : next.slot)
		{
			if (slot.nodeIndex != nodeIndex)
				continue;

			// Rebased before the ramp moves: the phase the old parameter path already reached is
			// carried into `phase`, and the new path integrates from `now`. Without it the pose
			// pass would integrate the new path from the old reference and land where the old
			// record never was.
			slot.phase = SpacePhaseAt(slot, space, clips, now);
			slot.tRef  = now;

			slot.param0     = SlotParameterAt(slot, now);
			slot.param1     = parameter;
			slot.paramStart = now;
			slot.paramEnd   = now + duration;
		}

		return next;
	}
}
