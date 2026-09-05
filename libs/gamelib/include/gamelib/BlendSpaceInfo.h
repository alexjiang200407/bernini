#pragma once
#include <gamelib/ClipInfo.h>

namespace game
{
	/** One clip of a blend space: an index into the acquire's clip table, and where it plays alone. */
	struct BlendSpaceMemberInfo
	{
		uint32_t clipIndex = 0;
		float    parameter = 0.0f;
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
			const size_t last = members.size() - 1;

			if (parameter <= members.front().parameter)
				return clips[members.front().clipIndex].CycleSeconds();
			if (parameter >= members[last].parameter)
				return clips[members[last].clipIndex].CycleSeconds();

			for (size_t i = 0; i < last; ++i)
			{
				const float a = members[i].parameter;
				const float b = members[i + 1].parameter;
				if (parameter < b)
				{
					return std::lerp(
						clips[members[i].clipIndex].CycleSeconds(),
						clips[members[i + 1].clipIndex].CycleSeconds(),
						(parameter - a) / (b - a));
				}
			}

			return clips[members[last].clipIndex].CycleSeconds();
		}
	};
}
