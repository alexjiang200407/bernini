#include <assetlib/skinning.h>
#include <assetlib/toon_shading_rig.h>
#include <assetlib_structs/BToonShadingRig.h>
#include <assetlib_structs/Skeleton.h>
#include <core/err/util.h>
#include <cstdint>
#include <optional>

namespace assetlib
{
	std::optional<uint32_t>
	resolveHeadBone(const BToonShadingRig& rig, const Skeleton& skeleton)
	{
		if (rig.headBone.empty())
			return std::nullopt;

		const std::optional<uint32_t> found = findBone(skeleton, rig.headBone);
		if (!found.has_value())
		{
			core::throw_runtime_error(
				"toon shading rig: the head bone '{}' is not in the skeleton",
				rig.headBone);
		}
		return found;
	}
}
