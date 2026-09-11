#include "plant_bake.h"
#include <core/file/IFileSystem.h>

#include <assetlib/avatar.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Skeleton.h>
#include <span>

namespace assetlib
{
	bool
	plantWeightsEmpty(const PlantWeights& weights) noexcept
	{
		return weights.legCount == 0 || weights.weights.empty();
	}

	void
	groundClipsForRig(
		const core::file::IFileSystem&   files,
		AnimationSet&                    clips,
		const std::span<const BMesh>     meshes,
		const Skeleton&                  skeleton,
		const std::span<const ClipFloor> authored)
	{
		groundClips(
			clips,
			meshes,
			skeleton,
			authored,
			avatarForRig(files, clips.skeleton, skeleton).legs);
	}

	void
	bakePlantWeightsForRig(
		const core::file::IFileSystem& files,
		AnimationSet&                  clips,
		const std::span<const BMesh>   meshes,
		const Skeleton&                skeleton)
	{
		bakePlantWeights(clips, meshes, skeleton, avatarForRig(files, clips.skeleton, skeleton));
	}
}
