#include "scene/Scene.h"
#include "scene/toon_shading_rig_record.h"
#include <bgl/IScene.h>
#include <bgl/glm.h>
#include <bgl/idl/ToonShadingRig.h>
#include <bgl/types/ToonShadingRigDesc.h>
#include <bgl/types/ToonShadingRigHandle.h>
#include <cmath>
#include <core/containers/multi_slot_handle.h>
#include <core/containers/slot_handle.h>
#include <core/math.h>
#include <cstdint>
#include <format>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

namespace bgl
{
	namespace
	{
		constexpr float c_HalfPi = std::numbers::pi_v<float> / 2.0f;

		[[noreturn]] void
		Refuse(const std::string_view what)
		{
			throw SceneError(std::format("AddToonShadingRig: {}", what));
		}

		[[nodiscard]] bool
		IsInvertibleAffine(const glm::mat4& m) noexcept
		{
			for (int c = 0; c < 4; ++c)
			{
				if (!core::is_finite(m[c]))
				{
					return false;
				}
			}
			if (m[0][3] != 0.0f || m[1][3] != 0.0f || m[2][3] != 0.0f || m[3][3] != 1.0f)
			{
				return false;
			}
			const float det = glm::determinant(glm::mat3(m));
			return std::isfinite(det) && std::abs(det) > 1e-12f;
		}

		void
		ValidateKey(const ToonShadingRigKeyDesc& key, const size_t edit, const size_t index)
		{
			const auto refuse = [&](const std::string_view what) {
				Refuse(std::format("edit {} key {}: {}", edit, index, what));
			};

			// The squared length is what normalizing divides by: a finite light can still overflow it.
			const float lightLengthSquared = glm::dot(key.light, key.light);
			if (!core::is_finite(key.light) || !std::isfinite(lightLengthSquared) ||
			    lightLengthSquared == 0.0f)
			{
				refuse("the light must be finite and nonzero, and its squared length finite");
			}
			if (!core::is_finite(key.position))
			{
				refuse("the position must be finite");
			}
			if (!std::isfinite(key.gain) || !std::isfinite(key.bend) || !std::isfinite(key.bulge) ||
			    !std::isfinite(key.rotation))
			{
				refuse("gain, bend, bulge and rotation must be finite");
			}
			if (!std::isfinite(key.size) || key.size <= 0.0f)
			{
				refuse("the size must be finite and positive");
			}
			if (!std::isfinite(key.radius) || key.radius < 0.0f)
			{
				refuse("the radius must be finite and non-negative");
			}
			if (!std::isfinite(key.anisotropy) || key.anisotropy < 0.0f || key.anisotropy >= 1.0f)
			{
				refuse("the anisotropy must be in [0, 1)");
			}
			if (!core::is_unit_interval(key.sharpness) ||
			    !core::is_unit_interval(key.normalSmoothing))
			{
				refuse("sharpness and normal smoothing must be in [0, 1]");
			}
		}

		void
		ValidateToonShadingRig(const ToonShadingRigDesc& desc)
		{
			uint32_t slots = 0;
			for (size_t e = 0; e < desc.edits.size(); ++e)
			{
				const ToonShadingRigEditDesc& edit = desc.edits[e];
				if (edit.keys.empty() || edit.keys.size() > cMaxToonShadingRigKeysPerEdit)
				{
					Refuse(
						std::format(
							"edit {} has {} keys, outside [1, {}]",
							e,
							edit.keys.size(),
							cMaxToonShadingRigKeysPerEdit));
				}
				if (!std::isfinite(edit.keySharpness) || edit.keySharpness <= 0.0f)
				{
					Refuse(std::format("edit {}'s key sharpness must be finite and positive", e));
				}
				for (size_t k = 0; k < edit.keys.size(); ++k)
				{
					ValidateKey(edit.keys[k], e, k);
				}
				slots += edit.mirrored ? 2u : 1u;
			}
			if (slots > cMaxToonShadingRigSlots)
			{
				Refuse(
					std::format(
						"the edits take {} slots, more than the {} one rig may",
						slots,
						cMaxToonShadingRigSlots));
			}

			if (!std::isfinite(desc.headRadius) || desc.headRadius <= 0.0f)
			{
				Refuse("headRadius must be finite and positive");
			}
			if (!std::isfinite(desc.fadeStartPixels) || !std::isfinite(desc.fadeEndPixels) ||
			    desc.fadeEndPixels < 0.0f || desc.fadeEndPixels >= desc.fadeStartPixels)
			{
				Refuse("the fade must be 0 <= fadeEndPixels < fadeStartPixels");
			}

			const FaceLightDesc& light = desc.faceLight;
			if (!std::isfinite(light.minElevation) || !std::isfinite(light.maxElevation) ||
			    light.minElevation < -c_HalfPi || light.minElevation > light.maxElevation ||
			    light.maxElevation > c_HalfPi)
			{
				Refuse("the face light's elevations must be -pi/2 <= min <= max <= pi/2");
			}
			if (!std::isfinite(light.maxAzimuth) || light.maxAzimuth < 0.0f ||
			    light.maxAzimuth > std::numbers::pi_v<float>)
			{
				Refuse("the face light's maxAzimuth must be in [0, pi]");
			}
			if (!std::isfinite(light.azimuthFadeStart) || !std::isfinite(light.azimuthFadeEnd) ||
			    light.azimuthFadeStart >= light.azimuthFadeEnd)
			{
				Refuse("the face light's azimuth fade must start before it ends");
			}
			if (!core::is_unit_interval(light.azimuthFadeAmount))
			{
				Refuse("the face light's azimuthFadeAmount must be in [0, 1]");
			}

			const FaceNormalDesc& normal = desc.faceNormal;
			if (!core::is_unit_interval(normal.smoothing))
			{
				Refuse("the face normal's smoothing must be in [0, 1]");
			}
			if (!core::is_finite(normal.radii) ||
			    !glm::all(glm::greaterThan(normal.radii, glm::vec3(0.0f))))
			{
				Refuse("the face normal's radii must be finite and positive");
			}

			if (desc.headBoneIndex == idl::cNoHeadBone)
			{
				Refuse(
					"headBoneIndex is the no-bone sentinel; leave it empty for the placement's "
					"frame");
			}

			if (!IsInvertibleAffine(desc.headToBone))
			{
				Refuse("headToBone must be a finite, invertible affine matrix");
			}
		}
	}

	ToonShadingRigHandle
	Scene::AddToonShadingRig(const ToonShadingRigDesc& desc)
	{
		ValidateToonShadingRig(desc);

		auto [record, keys] = PackToonShadingRig(desc);

		core::multi_slot_handle keyRange;
		if (!keys.empty())
		{
			keyRange = m_ToonShadingRigKeys.Add(std::span<const idl::ToonShadingRigKey>(keys));
		}
		record.keys = keyRange;

		try
		{
			const core::slot_handle entry = m_ToonShadingRigs.Add(record);

			ToonShadingRigMeta& meta = m_ToonShadingRigs.MetaAt(entry.index);
			meta.headBoneIndex       = desc.headBoneIndex;
			meta.useCount            = 0;
			return ToonShadingRigHandle{ entry };
		}
		catch (...)
		{
			if (!keyRange.is_null())
			{
				m_ToonShadingRigKeys.Erase(keyRange);
			}
			throw;
		}
	}

	void
	Scene::DeleteToonShadingRig(const ToonShadingRigHandle rig)
	{
		const ToonShadingRigMeta* meta = FindToonShadingRig(rig);
		if (meta == nullptr)
		{
			throw SceneError(
				"ToonShadingRigHandle passed to DeleteToonShadingRig is null, or already deleted");
		}
		if (meta->useCount > 0)
		{
			throw SceneError(
				"ToonShadingRigHandle passed to DeleteToonShadingRig is still held by a placement");
		}

		const idl::ToonShadingRig& record = m_ToonShadingRigs[rig.handle];
		if (!record.keys.Null())
		{
			m_ToonShadingRigKeys.EraseByIndex(record.keys.offsetStart);
		}
		m_ToonShadingRigs.Erase(rig.handle);
	}

	const ToonShadingRigMeta*
	Scene::FindToonShadingRig(const ToonShadingRigHandle rig) const noexcept
	{
		if (!rig.IsValid() || !m_ToonShadingRigs.IsValid(rig.handle))
		{
			return nullptr;
		}
		return &m_ToonShadingRigs.MetaAt(rig.handle.index);
	}

	void
	Scene::AcquireToonShadingRig(const ToonShadingRigHandle rig) noexcept
	{
		++m_ToonShadingRigs.MetaAt(rig.handle.index).useCount;
	}

	void
	Scene::ReleaseToonShadingRig(const ToonShadingRigHandle rig) noexcept
	{
		if (!rig.IsValid() || !m_ToonShadingRigs.IsValid(rig.handle))
		{
			return;
		}
		uint32_t& useCount = m_ToonShadingRigs.MetaAt(rig.handle.index).useCount;
		if (useCount > 0)
		{
			--useCount;
		}
	}
}
