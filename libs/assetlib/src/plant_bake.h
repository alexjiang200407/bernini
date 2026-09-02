#pragma once
#include <core/file/IFileSystem.h>

namespace assetlib
{
	struct AnimationSet;
	struct BMesh;
	struct Skeleton;

	/**
	 * Bakes `clips`' plant weights against the avatar its rig authors, if there is one.
	 *
	 * The whole of what a cook has to do about foot planting, in one place because it is one rule:
	 * the avatar is found by convention from `clips.skeleton` (`avatarKeyFor`), its bone names are
	 * resolved against `skeleton`, and the weights are measured over the clips. A rig with no avatar
	 * -- which is most of them -- costs one stat.
	 *
	 * **After grounding.** The weights are measured against `y = 0`, which is where `groundClips`
	 * puts a clip's lowest pose, so a call before it plants nothing.
	 *
	 * An avatar that will not parse, or that names bones this rig does not carry, leaves the file
	 * without weights and says so in the log: the weights are derived data and a load measures
	 * instead, but an authored file being ignored is not something to pass over in silence.
	 */
	void
	bakePlantWeightsForRig(
		const core::file::IFileSystem& files,
		AnimationSet&                  clips,
		std::span<const BMesh>         meshes,
		const Skeleton&                skeleton);
}
