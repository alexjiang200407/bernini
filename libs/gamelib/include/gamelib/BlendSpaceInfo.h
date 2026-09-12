#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gamelib/ClipInfo.h>
#include <span>
#include <string>
#include <vector>

namespace game
{
	/** One clip of a blend space: an index into the acquire's clip table, and where it plays alone. */
	struct BlendSpaceSampleInfo
	{
		uint32_t clipIndex = 0;
		float    parameter = 0.0f;
	};

	/**
	 * The two samples a parameter sits between, and how far between them it sits.
	 *
	 * `weight` is the *upper* sample's; the lower carries `1 - weight`. Past the top of the run both
	 * name the last sample; below the bottom they are the first two with `weight` zero. So the
	 * sample playing alone at either end is `lower`, and `upper` means nothing without `weight`.
	 */
	struct BlendSpaceStraddle
	{
		size_t lower  = 0;  // into BlendSpaceInfo::samples
		size_t upper  = 0;
		float  weight = 0.0f;
	};

	/**
	 * One blend space of an acquired rig, in source-asset terms.
	 *
	 * A playback slot names a *node*, and the rig's node table is every clip in clip order and then
	 * these -- so `clips[i]` is node `i`, and `spaces[i]` is node `clips.size() + i`. That ordering
	 * is what lets a slot name a clip the way it always did, and it is why a space needs no index of
	 * its own here.
	 *
	 * The samples are here rather than summarized because retargeting a parameter has to know them:
	 * a space's phase advances at the reciprocal of the weighted cycle length, so moving the
	 * parameter changes the rate, and holding the phase continuous across the write means
	 * integrating what the old parameter path already covered.
	 */
	struct BlendSpaceInfo
	{
		std::string name;

		// In strictly increasing parameter order, two or more.
		std::vector<BlendSpaceSampleInfo> samples;

		/** The parameter range authored. Outside it the end sample plays alone. */
		[[nodiscard]] float
		ParameterMin() const noexcept
		{
			return samples.front().parameter;
		}

		[[nodiscard]] float
		ParameterMax() const noexcept
		{
			return samples.back().parameter;
		}

		/**
		 * The cycle the space advances its shared phase by at `parameter`: its two straddling
		 * samples' cycles, weighted between them. Outside the authored range it is the end sample's
		 * alone.
		 *
		 * `clips` is the acquire's clip table, which is what `clipIndex` addresses. This is the same
		 * quantity the pose pass computes on the GPU, and the reason a parameter that moves changes
		 * how fast the phase advances.
		 */
		[[nodiscard]] float
		SecondsAt(std::span<const ClipInfo> clips, float parameter) const
		{
			const BlendSpaceStraddle at = StraddleAt(parameter);

			return std::lerp(
				clips[samples[at.lower].clipIndex].CycleSeconds(),
				clips[samples[at.upper].clipIndex].CycleSeconds(),
				at.weight);
		}

		/**
		 * Which two samples `parameter` lands between, and how far between them -- what an author
		 * reads off a space to see which clips are live under the cursor.
		 *
		 * The CPU twin of `SpaceSamples` in `PoseSkinned.slang`, walked rather than searched for
		 * the same reason: a space is a handful of samples. Like `SecondsAt` it is a *twin*, and
		 * nothing mechanically holds the two in step -- a readout that disagreed with the pose on
		 * screen is the failure this shape exists to make visible rather than to rule out.
		 */
		[[nodiscard]] BlendSpaceStraddle
		StraddleAt(float parameter) const noexcept
		{
			const size_t last = samples.size() - 1;

			if (parameter >= samples[last].parameter)
				return { last, last, 0.0f };

			for (size_t i = 0; i < last; ++i)
			{
				const float b = samples[i + 1].parameter;
				if (parameter < b)
				{
					const float a = samples[i].parameter;
					return { i, i + 1, std::clamp((parameter - a) / (b - a), 0.0f, 1.0f) };
				}
			}

			return { last, last, 0.0f };
		}
	};
}
