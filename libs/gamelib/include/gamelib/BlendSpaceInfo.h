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
	struct BlendSpaceMemberInfo
	{
		uint32_t clipIndex = 0;
		float    parameter = 0.0f;
	};

	/**
	 * The two members a parameter sits between, and how far between them it sits.
	 *
	 * `weight` is the *upper* member's; the lower carries `1 - weight`. Outside the authored range
	 * both name the end member and `weight` is zero, which is that member playing alone whichever
	 * of the two a caller reads.
	 */
	struct BlendSpaceStraddle
	{
		size_t lower  = 0;  // into BlendSpaceInfo::members
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
	 * The members are here rather than summarized because retargeting a parameter has to know them:
	 * a space's phase advances at the reciprocal of the weighted cycle length, so moving the
	 * parameter changes the rate, and holding the phase continuous across the write means
	 * integrating what the old parameter path already covered.
	 */
	struct BlendSpaceInfo
	{
		std::string name;

		// In strictly increasing parameter order, two or more.
		std::vector<BlendSpaceMemberInfo> members;

		/** The parameter range authored. Outside it the end member plays alone. */
		[[nodiscard]] float
		ParameterMin() const noexcept
		{
			return members.front().parameter;
		}

		[[nodiscard]] float
		ParameterMax() const noexcept
		{
			return members.back().parameter;
		}

		/**
		 * The cycle the space advances its shared phase by at `parameter`: its two straddling
		 * members' cycles, weighted between them. Outside the authored range it is the end member's
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
				clips[members[at.lower].clipIndex].CycleSeconds(),
				clips[members[at.upper].clipIndex].CycleSeconds(),
				at.weight);
		}

		/**
		 * Which two members `parameter` lands between, and how far between them -- what an author
		 * reads off a space to see which clips are live under the cursor.
		 *
		 * The CPU twin of `SpaceMembers` in `PoseSkinned.slang`, walked rather than searched for
		 * the same reason: a space is a handful of members. Like `SecondsAt` it is a *twin*, and
		 * nothing mechanically holds the two in step -- a readout that disagreed with the pose on
		 * screen is the failure this shape exists to make visible rather than to rule out.
		 */
		[[nodiscard]] BlendSpaceStraddle
		StraddleAt(float parameter) const noexcept
		{
			const size_t last = members.size() - 1;

			if (parameter >= members[last].parameter)
				return { last, last, 0.0f };

			for (size_t i = 0; i < last; ++i)
			{
				const float b = members[i + 1].parameter;
				if (parameter < b)
				{
					const float a = members[i].parameter;
					return { i, i + 1, std::clamp((parameter - a) / (b - a), 0.0f, 1.0f) };
				}
			}

			return { last, last, 0.0f };
		}
	};
}
