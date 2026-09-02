#include "plant_bake.h"

#include <assetlib/avatar.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/Skeleton.h>

#include <spdlog/spdlog.h>

namespace assetlib
{
	std::vector<AvatarLegChain>
	legChainsForRig(
		const core::file::IFileSystem& files,
		const AnimationSet&            clips,
		const Skeleton&                skeleton)
	{
		if (clips.skeleton.empty())
			return {};

		std::string key;
		try
		{
			key = avatarKeyFor(clips.skeleton);
		}
		catch (const std::exception&)
		{
			// A clip set naming a skeleton outside the skeletons directory: nothing this can act
			// on, and the reference scan is where a misplaced container gets reported.
			return {};
		}

		if (!files.Stat(key).has_value())
			return {};

		try
		{
			return resolveLegChains(loadAvatar(files, key), skeleton);
		}
		catch (const std::exception& e)
		{
			spdlog::warn(
				"'{}' cannot be used against '{}', so its clips carry no plant weights: {}",
				key,
				clips.skeleton,
				e.what());
			return {};
		}
	}

	void
	bakePlantWeightsForRig(
		const core::file::IFileSystem& files,
		AnimationSet&                  clips,
		const std::span<const BMesh>   meshes,
		const Skeleton&                skeleton)
	{
		bakePlantWeights(clips, meshes, skeleton, legChainsForRig(files, clips, skeleton));
	}
}
