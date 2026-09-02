#pragma once

namespace assetlib
{
	class AssetStore;

	struct AnimationSet;
	struct ClipFloor;
	struct Skeleton;

	/**
	 * Grounds `clips` against every mesh in `store` that skins to `rigRel`, then bakes each of their
	 * posed boxes. Clips arriving without their geometry have neither a floor nor a box to measure
	 * otherwise, and a load that has to measure one is the cost this bake exists to remove.
	 *
	 * Grounding comes first because a box measured before it describes a rig standing where the
	 * runtime never draws it, and `authored` has to reach it or a second pass would re-measure a
	 * floor the author deliberately overruled.
	 *
	 * Each mesh comes through `LoadRegenMesh`, not its bytes on disk: the source re-export that
	 * stales a clip set stales its geometry with it, and a floor measured off the stale copy would
	 * move the rig to where that geometry used to be.
	 *
	 * The plant weights ride here too, for the same reason the boxes do and against the same mesh
	 * set: a sole is fitted over every mesh that carries the foot, so a rig drawn as a body and a
	 * boot measures once over both.
	 *
	 * By path rather than by signature: FindMatchingSkeleton has already refused a project where
	 * two skeletons share a signature, so a mesh reaching this rig by signature names exactly
	 * this file. A mesh that will not load contributes no box rather than failing the bake.
	 */
	void
	bakeBoundsForRig(
		const AssetStore&          store,
		AnimationSet&              clips,
		const std::string&         rigRel,
		const Skeleton&            skeleton,
		std::span<const ClipFloor> authored = {});
}
