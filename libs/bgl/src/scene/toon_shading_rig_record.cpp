#include "scene/toon_shading_rig_record.h"
#include <bgl/glm.h>
#include <bgl/idl/ToonShadingRig.h>
#include <bgl/types/ToonShadingRigDesc.h>
#include <cstddef>
#include <cstdint>

namespace bgl
{
	namespace
	{
		/** The edit's sign lock, as its keys' gains have it: of one sign, or free. */
		[[nodiscard]] uint32_t
		SignLock(const ToonShadingRigEditDesc& edit) noexcept
		{
			bool anyLight = false;
			bool anyShade = false;
			for (const ToonShadingRigKeyDesc& key : edit.keys)
			{
				anyLight = anyLight || key.gain > 0.0f;
				anyShade = anyShade || key.gain < 0.0f;
			}
			if (anyShade && !anyLight)
			{
				return idl::cToonShadingRigEditShadeOnly;
			}
			if (anyLight && !anyShade)
			{
				return idl::cToonShadingRigEditLightOnly;
			}
			return 0u;
		}
	}

	PackedToonShadingRig
	PackToonShadingRig(const ToonShadingRigDesc& desc)
	{
		auto packed = PackedToonShadingRig();

		idl::ToonShadingRig& record = packed.record;

		uint32_t slots = 0;
		for (size_t e = 0; e < desc.edits.size(); ++e)
		{
			const ToonShadingRigEditDesc& edit = desc.edits[e];

			idl::ToonShadingRigEdit& entry = record.edits[e];
			entry.firstKey                 = static_cast<uint32_t>(packed.keys.size());
			entry.keyCount                 = static_cast<uint32_t>(edit.keys.size());
			entry.keySharpness             = edit.keySharpness;
			entry.flags = SignLock(edit) | (edit.mirrored ? idl::cToonShadingRigEditMirrored : 0u);

			for (const ToonShadingRigKeyDesc& key : edit.keys)
			{
				auto row            = idl::ToonShadingRigKey();
				row.lightAndGain    = glm::vec4(glm::normalize(key.light), key.gain);
				row.positionAndSize = glm::vec4(key.position, key.size);
				row.shape           = glm::vec4(key.anisotropy, key.sharpness, key.bend, key.bulge);
				row.rotationRadiusSmoothing =
					glm::vec4(key.rotation, key.radius, key.normalSmoothing, 0.0f);
				packed.keys.emplace_back(row);
			}
			slots += edit.mirrored ? 2u : 1u;
		}

		const glm::mat4 rows = glm::transpose(desc.headToBone);
		record.headToBone[0] = rows[0];
		record.headToBone[1] = rows[1];
		record.headToBone[2] = rows[2];

		record.headBoneIndex     = desc.headBoneIndex.value_or(idl::cNoHeadBone);
		record.editCount         = static_cast<uint32_t>(desc.edits.size());
		record.slotCount         = slots;
		record.headRadius        = desc.headRadius;
		record.fadeStartPixels   = desc.fadeStartPixels;
		record.fadeEndPixels     = desc.fadeEndPixels;
		record.minElevation      = desc.faceLight.minElevation;
		record.maxElevation      = desc.faceLight.maxElevation;
		record.maxAzimuth        = desc.faceLight.maxAzimuth;
		record.azimuthFadeStart  = desc.faceLight.azimuthFadeStart;
		record.azimuthFadeEnd    = desc.faceLight.azimuthFadeEnd;
		record.azimuthFadeAmount = desc.faceLight.azimuthFadeAmount;

		return packed;
	}
}
