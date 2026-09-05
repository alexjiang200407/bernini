#pragma once

namespace game
{
	/**
	 * One blend space of an acquired rig, in source-asset terms.
	 *
	 * A playback slot names a *node*, and the rig's node table is every clip in clip order and then
	 * these -- so `clips[i]` is node `i`, and `spaces[i]` is node `clips.size() + i`. That ordering
	 * is what lets a slot name a clip the way it always did, and it is why a space needs no index of
	 * its own here.
	 */
	struct BlendSpaceInfo
	{
		std::string name;

		/**
		 * The parameter range the space is authored over. Outside it the end member plays alone, so
		 * this is the span worth moving across rather than a rule about what is accepted.
		 */
		float parameterMin = 0.0f;
		float parameterMax = 0.0f;
	};
}
