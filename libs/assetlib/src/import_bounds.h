#pragma once

namespace assetlib
{
	struct AnimationSet;
	struct Skeleton;

	/**
	 * Bakes the posed box of every mesh in the project that skins to `rigRel`, whose skeleton is
	 * `skeleton`. Clips arriving without their geometry have no box to measure otherwise, and a
	 * load that has to measure one is the cost this bake exists to remove.
	 *
	 * By path rather than by signature: FindMatchingSkeleton has already refused a project where
	 * two skeletons share a signature, so a mesh reaching this rig by signature names exactly
	 * this file. A mesh that will not load contributes no box rather than failing the bake.
	 */
	void
	bakeBoundsForRig(
		AnimationSet&                clips,
		const std::filesystem::path& dataRoot,
		const std::string&           rigRel,
		const Skeleton&              skeleton);
}
