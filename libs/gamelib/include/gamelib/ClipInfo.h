#pragma once

namespace game
{
	/**
	 * One playable clip of an acquired mesh, in source-asset terms. Its index in the acquire's clip
	 * table is what an instance names.
	 *
	 * Shared by both animated tiers: a VAT bake and a skinned rig describe a clip identically, so a
	 * caller showing a clip list does not have to know which door it came through.
	 */
	struct ClipInfo
	{
		std::string name;
		uint32_t    frameCount = 0;
		float       sampleRate = 30.0f;
		float       duration   = 0.0f;
		bool        loop       = false;
	};
}
