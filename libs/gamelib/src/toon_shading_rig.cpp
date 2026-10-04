#include "toon_shading_rig.h"
#include <assetlib/skinning.h>
#include <assetlib/toon_shading_rig.h>
#include <assetlib_structs/BToonShadingRig.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/types/ToonShadingRigDesc.h>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace game
{
	bgl::ToonShadingRigDesc
	ToonShadingRigDescOf(
		const assetlib::BToonShadingRig& rig,
		const assetlib::Skeleton*        skeleton,
		const ToonShadingRigPose         pose)
	{
		auto desc = bgl::ToonShadingRigDesc()
		                .SetHeadToBone(rig.headToBone)
		                .SetHeadRadius(rig.headRadius)
		                .SetFadeStartPixels(rig.fadeStartPixels)
		                .SetFadeEndPixels(rig.fadeEndPixels)
		                .SetFaceLight(
							bgl::FaceLightDesc()
								.SetMinElevation(glm::radians(rig.faceLight.minElevation))
								.SetMaxElevation(glm::radians(rig.faceLight.maxElevation))
								.SetMaxAzimuth(glm::radians(rig.faceLight.maxAzimuth))
								.SetAzimuthFadeStart(glm::radians(rig.faceLight.azimuthFadeStart))
								.SetAzimuthFadeEnd(glm::radians(rig.faceLight.azimuthFadeEnd))
								.SetAzimuthFadeAmount(rig.faceLight.azimuthFadeAmount));

		if (!rig.headBone.empty())
		{
			if (skeleton == nullptr)
			{
				core::throw_runtime_error(
					"toon shading rig: the head bone '{}' is named, but the mesh has no skeleton",
					rig.headBone);
			}

			const std::optional<uint32_t> bone = assetlib::resolveHeadBone(rig, *skeleton);
			if (pose == ToonShadingRigPose::kPosed)
				desc.headBoneIndex = bone;
			else
				desc.headToBone =
					assetlib::bindPoseModelTransforms(*skeleton)[*bone] * desc.headToBone;
		}

		desc.edits.reserve(rig.edits.size());
		for (const assetlib::ToonShadingRigEdit& authored : rig.edits)
		{
			auto keys = std::vector<bgl::ToonShadingRigKeyDesc>();
			keys.reserve(authored.keys.size());
			for (const assetlib::ToonShadingRigKey& key : authored.keys)
			{
				keys.push_back(
					bgl::ToonShadingRigKeyDesc()
						.SetLight(key.light)
						.SetPosition(key.position)
						.SetGain(key.gain)
						.SetSize(key.size)
						.SetAnisotropy(key.anisotropy)
						.SetSharpness(key.sharpness)
						.SetBend(key.bend)
						.SetBulge(key.bulge)
						.SetRotation(glm::radians(key.rotation))
						.SetRadius(key.radius)
						.SetNormalSmoothing(key.normalSmoothing));
			}

			desc.edits.push_back(
				bgl::ToonShadingRigEditDesc()
					.SetKeys(std::move(keys))
					.SetKeySharpness(authored.keySharpness)
					.SetMirrored(authored.mirrored));
		}

		return desc;
	}
}
