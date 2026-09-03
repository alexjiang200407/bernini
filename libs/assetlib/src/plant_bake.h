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
	struct ClipFloor;

	/**
	 * Grounds `clips` against the avatar its rig authors, if there is one.
	 *
	 * The pair of bakePlantWeightsForRig below, and called before it: a rig that authored legs is
	 * grounded on its lowest *sole* rather than its lowest vertex, because a plant applies the
	 * ground's departure from the floor a clip was authored on and that floor has to be the one the
	 * feet are on. The test Dog is the worked example -- its lowest vertex is not a foot, and
	 * grounding on it left the `Idle` soles 1 and 4 cm up, which every planted foot then hovered by.
	 *
	 * A rig with no avatar grounds exactly as it did before, on the lowest vertex any mesh reaches.
	 */
	void
	groundClipsForRig(
		const core::file::IFileSystem& files,
		AnimationSet&                  clips,
		std::span<const BMesh>         meshes,
		const Skeleton&                skeleton,
		std::span<const ClipFloor>     authored = {});

	void
	bakePlantWeightsForRig(
		const core::file::IFileSystem& files,
		AnimationSet&                  clips,
		std::span<const BMesh>         meshes,
		const Skeleton&                skeleton);
}
