#pragma once

namespace assetlib
{
	/**
	 * One clip of a blend space, and the parameter value at which it plays alone.
	 *
	 * The clip is named and not indexed, for the reason an avatar names its bones: an index is a
	 * fact about one cook of one `.banim`, and a re-import that adds a clip would shift every index
	 * after it, silently. Resolution happens where both name tables are in hand.
	 */
	struct BlendSpaceMember
	{
		std::string clip;
		float       parameter = 0.0f;

		bool
		operator==(const BlendSpaceMember&) const = default;
	};

	/** One 1D blend space: its clips in parameter order, blended by a parameter. */
	struct BlendSpace
	{
		std::string                   name;
		std::vector<BlendSpaceMember> members;

		bool
		operator==(const BlendSpace&) const = default;
	};

	/**
	 * The blend spaces authored against one clip set.
	 *
	 * Authored, so this is the asset: canonical JSON under `Data/Authored/`, no magic and no bake
	 * token, and nothing cooks it into anything. It holds no single-clip entries -- every clip of
	 * the rig is already a node under its own name, so an authored one would be a second name for
	 * the same thing.
	 *
	 * `animations` is the one `.banim` every member names a clip of, stored as a path so a rename
	 * rewrites it. A set is authored against exactly one clip set, which is what lets the resolved
	 * tables hang off the rig that clip set created.
	 */
	struct BlendSet
	{
		std::string name;
		std::string animations;  // path to a `.banim`, relative to the data root

		std::vector<BlendSpace> spaces;

		std::string extraJson = "{}";

		bool
		operator==(const BlendSet&) const = default;
	};

	/**
	 * @throws std::runtime_error if a space is unnamed, two spaces share a name, a space holds
	 *         fewer than two members, a member names no clip, or its parameters are not strictly
	 *         increasing.
	 *
	 * Strictly increasing rather than merely sorted: two members at one parameter have no defined
	 * weighting between them, and a set that reached the GPU would divide by a zero span.
	 *
	 * What is *not* checked here is whether a named clip exists or loops. Neither is knowable
	 * without the `.banim`, and both are checked where the two meet.
	 */
	void
	validateBlendSet(const BlendSet& set);
}
