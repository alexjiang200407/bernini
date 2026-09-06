#pragma once

namespace bgl
{
	/**
	 * One clip of a blend space, resolved: an index into the rig's clip table and the parameter
	 * value at which that clip plays alone.
	 *
	 * bgl never reads a `.bblend` and never resolves a clip name: a name is matched against the
	 * clip set's own string pool, which lives in assetlib. Whoever loaded the containers fills this
	 * in -- gamelib's acquire -- exactly as it does for FootPlantDesc.
	 */
	struct BlendSpaceMemberDesc
	{
		uint32_t clipIndex = 0;
		float    parameter = 0.0f;
	};

	/**
	 * One 1D blend space: its clips in strictly increasing parameter order.
	 *
	 * Two members is the floor -- one is a clip, and every clip is already a node under its own
	 * index. Every member must be a looping clip: the parameter sets a shared normalized phase, and
	 * a clip that clamps rather than wraps would sit on its last frame while the others cycle.
	 */
	struct BlendSpaceDesc
	{
		std::vector<BlendSpaceMemberDesc> members;
	};

	/**
	 * The blend spaces a rig carries, or nothing at all -- the default is a rig whose only nodes are
	 * its clips, which is every rig whose clip set has no `.bblend`.
	 *
	 * These append *after* the clip nodes `AddRig` synthesizes, so space `i` is node
	 * `clipCount + i` and no clip's node index moves when a set is added. That ordering is what lets
	 * `SkinnedInstanceDesc::clip` keep meaning what it always did.
	 */
	struct BlendSetDesc
	{
		std::vector<BlendSpaceDesc> spaces;
	};
}
