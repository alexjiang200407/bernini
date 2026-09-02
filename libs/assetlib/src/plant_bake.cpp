#include "plant_bake.h"

#include <assetlib/avatar.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Skeleton.h>

namespace assetlib
{
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
