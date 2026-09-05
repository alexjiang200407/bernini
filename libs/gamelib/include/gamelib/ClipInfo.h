#pragma once

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
		 * How long one cycle of this clip lasts, in seconds: the intervals it wraps over, at its
		 * authored rate. `frameCount` counts both ends, so a two-frame clip spans one interval.
		 *
		 * What a blend space advances its shared phase by. Meaningful on a clip that does not loop
		 * too -- it is just the clip's length then, since nothing wraps.
		 */
		[[nodiscard]] float
		CycleSeconds() const noexcept
		{
			return static_cast<float>(frameCount - 1) / sampleRate;
		}
	};
}
