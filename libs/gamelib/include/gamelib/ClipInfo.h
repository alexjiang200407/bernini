#pragma once

#include <cstdint>
#include <string>
namespace game
{
	/**
	 * One playable clip of an acquired mesh, in source-asset terms. Its index in the acquire's clip
	 * table is what an instance names.
	 *
	 * Shared by both pose sources: the rig describes a clip once however it is played, so a
	 * caller showing a clip list does not have to know which door it came through.
	 */
	struct ClipInfo
	{
		std::string name;
		uint32_t    frameCount = 0;
		float       sampleRate = 30.0f;
		float       duration   = 0.0f;
		bool        loop       = false;

		/**
		 * How fast the root travels over the ground through this clip, in units per second, as
		 * measured at cook. Zero for a clip that does not travel -- an idle, or a turn in place.
		 *
		 * What a locomotion blend space's thresholds *are*: a walk at 1.4 and a run at 4.2 blend
		 * correctly at 2.8 precisely because those are the speeds they were animated at. Carried
		 * here rather than read back off the `.banim` so a caller that already has the clip table
		 * needs no second decode for one float per clip.
		 */
		float locomotionSpeed = 0.0f;

		/**
		 * How long one cycle of this clip lasts, in seconds: the intervals it wraps over, at its
		 * authored rate. `frameCount` counts both ends, so a two-frame clip spans one interval.
		 *
		 * What a blend space advances its shared phase by. Meaningful on a clip that does not loop
		 * too: a space wraps every sample by it, whatever the clip's own flag says.
		 */
		[[nodiscard]] float
		CycleSeconds() const noexcept
		{
			return static_cast<float>(frameCount - 1) / sampleRate;
		}
	};
}
