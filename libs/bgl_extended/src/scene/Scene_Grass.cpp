#include "scene/Scene.h"
#include <algorithm>
#include <bgl/GrassHandle.h>
#include <bgl/IScene.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialType.h>
#include <bgl/glm.h>
#include <bgl/types/GrassDesc.h>
#include <cmath>
#include <core/math.h>
#include <cstdint>
#include <format>
#include <string_view>

namespace bgl
{
	namespace
	{
		constexpr uint32_t c_InitialGrassLooks = 4;

		[[nodiscard]] bool
		IsShare(const float value) noexcept
		{
			return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
		}

		[[nodiscard]] bool
		IsPositive(const float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		[[nodiscard]] bool
		IsNonNegative(const float value) noexcept
		{
			return std::isfinite(value) && value >= 0.0f;
		}

		[[nodiscard]] bool
		IsColor(const glm::vec3& color) noexcept
		{
			return core::is_finite(color) && color.x >= 0.0f && color.y >= 0.0f && color.z >= 0.0f;
		}
	}

	void
	Scene::ValidateGrass(const GrassDesc& desc, const std::string_view caller)
	{
		const auto refuse = [caller](const std::string_view why) {
			throw SceneError(std::format("{}: {}", caller, why));
		};

		const MaterialType kind = desc.material.materialType;
		if (!desc.material.IsValid() || kind == MaterialType::kNull ||
		    kind == MaterialType::kAssert)
		{
			refuse("the material must be a drawable one (not null, kNull or kAssert)");
		}
		if (desc.material.layerType == LayerType::kBlend)
		{
			refuse("a blended material has no opaque program for a solid blade to draw through");
		}

		const GrassBladeDesc& blade = desc.blade;
		if (!IsPositive(blade.minHeight) || !IsPositive(blade.maxHeight) ||
		    blade.minHeight > blade.maxHeight)
		{
			refuse("blade heights must be finite, positive and minHeight <= maxHeight");
		}
		if (!IsPositive(blade.rootWidth))
		{
			refuse("blade.rootWidth must be finite and positive");
		}
		if (!IsShare(blade.tipWidth) || !IsShare(blade.curvature) || !IsShare(blade.lean))
		{
			refuse("blade.tipWidth, curvature and lean must each be in [0, 1]");
		}
		if (blade.farSegments < 1 || blade.farSegments > blade.nearSegments ||
		    blade.nearSegments > c_MaxGrassBladeSegments)
		{
			refuse(
				std::format(
					"blade segments must be 1 <= farSegments <= nearSegments <= {}",
					c_MaxGrassBladeSegments));
		}

		if (desc.clump.bladesPerClump < 1 || desc.clump.bladesPerClump > c_MaxGrassBladesPerClump)
		{
			refuse(
				std::format("clump.bladesPerClump must be in [1, {}]", c_MaxGrassBladesPerClump));
		}
		if (!IsNonNegative(desc.clump.radius))
		{
			refuse("clump.radius must be finite and non-negative");
		}

		const GrassDensityDesc& density = desc.density;
		if (!IsNonNegative(density.fadeStart) || !std::isfinite(density.fadeEnd) ||
		    density.fadeEnd <= density.fadeStart)
		{
			refuse("density fades must be finite, non-negative and fadeStart < fadeEnd");
		}
		if (!IsNonNegative(density.widening))
		{
			refuse("density.widening must be finite and non-negative");
		}

		if (!IsShare(desc.response.stiffness) || !IsNonNegative(desc.response.gustResponse))
		{
			refuse(
				"response.stiffness must be in [0, 1] and response.gustResponse finite and "
				"non-negative");
		}

		const GrassLightingDesc& lighting = desc.lighting;
		if (!IsShare(lighting.rootOcclusion) || !IsShare(lighting.normalRounding) ||
		    !IsShare(lighting.groundNormalNear) || !IsShare(lighting.groundNormalFar))
		{
			refuse(
				"lighting.rootOcclusion, normalRounding and both ground-normal blends must be "
				"in [0, 1]");
		}
		if (!IsColor(lighting.translucencyColor) || !IsNonNegative(lighting.translucency))
		{
			refuse("lighting translucency and its colour must be finite and non-negative");
		}

		if (!IsColor(desc.color.rootTint) || !IsColor(desc.color.tipTint) ||
		    !IsShare(desc.color.variation))
		{
			refuse("colour tints must be finite and non-negative, and variation in [0, 1]");
		}
	}

	GrassHandle
	Scene::CreateGrass(const GrassDesc& desc)
	{
		ValidateGrass(desc, "CreateGrass");

		const auto meta = GrassMeta{ .desc = desc };
		auto       slot = m_Grass.try_allocate_and_emplace(meta);
		if (slot.is_null())
		{
			m_Grass.grow(std::max(c_InitialGrassLooks, m_Grass.capacity() * 2));
			slot = m_Grass.allocate_and_emplace(meta);
		}

		return GrassHandle{ slot };
	}

	void
	Scene::UpdateGrass(const GrassHandle grass, const GrassDesc& desc)
	{
		if (!IsGrassAlive(grass))
		{
			throw SceneError(
				"GrassHandle passed to UpdateGrass refers to a deleted or unknown look");
		}

		ValidateGrass(desc, "UpdateGrass");

		m_Grass[grass.handle.index].desc = desc;
		++m_TemporalEpoch;
	}

	void
	Scene::DeleteGrass(const GrassHandle grass)
	{
		if (!IsGrassAlive(grass))
		{
			throw SceneError(
				"GrassHandle passed to DeleteGrass refers to a deleted or unknown look");
		}

		if (m_Grass[grass.handle.index].useCount > 0)
		{
			throw SceneError(
				"GrassHandle passed to DeleteGrass is still bound by a live geom; delete it first");
		}

		m_Grass.release_slot(grass.handle.index);
	}
}
