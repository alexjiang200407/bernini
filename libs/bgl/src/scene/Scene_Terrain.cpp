#include "scene/Scene.h"
#include <algorithm>
#include <assetlib_structs/Heightfield.h>
#include <bgl/IScene.h>
#include <bgl/MaterialType.h>
#include <bgl/glm.h>
#include <bgl/idl/Constants.h>
#include <bgl/types/LayerType.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/TerrainDesc.h>
#include <bgl/types/TerrainHandle.h>
#include <cmath>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>
#include <vector>

namespace bgl
{
	namespace
	{
		constexpr uint32_t c_InitialTerrains = 1;

		// One number declared twice, because a public header cannot reach the generated IDL.
		static_assert(c_MaxTerrainSamples == idl::cTerrainMaxSamples);

		// A patch's mesh group emits a (quads + 1)^2 grid, within the limits every mesh stage keeps.
		static_assert(
			idl::cTerrainPatchVertices ==
			(idl::cTerrainPatchQuads + 1) * (idl::cTerrainPatchQuads + 1));
		static_assert(
			idl::cTerrainPatchPrims == 2 * idl::cTerrainPatchQuads * idl::cTerrainPatchQuads);
		static_assert(idl::cTerrainPatchVertices <= idl::cMaxVerticesPerMeshlet);
		static_assert(idl::cTerrainPatchPrims <= idl::cMaxPrimsPerMeshlet);

		// The coarsest level's one node covers the widest heightfield allowed.
		static_assert(
			static_cast<uint64_t>(idl::cTerrainPatchQuads) << (idl::cTerrainMaxLevels - 1) >=
			idl::cTerrainMaxSamples - 1);

		[[nodiscard]] bool
		IsPositive(const float value) noexcept
		{
			return std::isfinite(value) && value > 0.0f;
		}

		void
		ValidateTerrain(const TerrainDesc& desc)
		{
			const auto refuse = [](const std::string_view why) {
				throw SceneError(std::format("CreateTerrain: {}", why));
			};

			if (desc.heightfield == nullptr)
			{
				refuse("the heightfield is null");
			}
			const assetlib::Heightfield& field = *desc.heightfield;

			if (field.samplesX < 2 || field.samplesZ < 2 || field.samplesX > c_MaxTerrainSamples ||
			    field.samplesZ > c_MaxTerrainSamples)
			{
				refuse(
					std::format(
						"the heightfield is {} x {} samples; each axis takes 2 to {}",
						field.samplesX,
						field.samplesZ,
						c_MaxTerrainSamples));
			}
			if (field.heights.size() !=
			    static_cast<size_t>(field.samplesX) * static_cast<size_t>(field.samplesZ))
			{
				refuse(
					std::format(
						"the heightfield holds {} samples where {} x {} are declared",
						field.heights.size(),
						field.samplesX,
						field.samplesZ));
			}
			if (!IsPositive(field.cellSize) || !IsPositive(field.heightRange))
			{
				refuse("cellSize and heightRange must be finite and positive");
			}
			if (!std::isfinite(field.minHeight) || !core::is_finite(desc.origin))
			{
				refuse("minHeight and origin must be finite");
			}
			if (!IsPositive(desc.pixelsPerCell))
			{
				refuse("pixelsPerCell must be finite and positive");
			}

			const MaterialType kind = desc.material.materialType;
			if (!desc.material.IsValid() || kind == MaterialType::kNull ||
			    kind == MaterialType::kAssert)
			{
				refuse("the material must be a drawable one (not null, kNull or kAssert)");
			}
			if (desc.material.layerType != LayerType::kOpaque)
			{
				refuse("the material must be in the opaque layer");
			}
		}
	}

	TerrainHandle
	Scene::CreateTerrain(const TerrainDesc& desc)
	{
		ValidateTerrain(desc);
		const assetlib::Heightfield& field = *desc.heightfield;

		auto meta          = TerrainMeta();
		meta.samplesX      = field.samplesX;
		meta.samplesZ      = field.samplesZ;
		meta.cellSize      = field.cellSize;
		meta.minHeight     = field.minHeight;
		meta.heightRange   = field.heightRange;
		meta.pixelsPerCell = desc.pixelsPerCell;
		meta.origin        = desc.origin;
		meta.material      = desc.material;
		meta.heights       = field.heights;

		auto slot = m_Terrains.try_allocate_and_emplace(std::move(meta));
		if (slot.is_null())
		{
			m_Terrains.grow(std::max(c_InitialTerrains, m_Terrains.capacity() * 2));
			slot = m_Terrains.allocate_and_emplace(std::move(meta));
		}

		++m_TerrainEpoch;
		return TerrainHandle{ slot };
	}

	void
	Scene::DeleteTerrain(const TerrainHandle terrain)
	{
		if (!IsTerrainAlive(terrain))
		{
			throw SceneError(
				"TerrainHandle passed to DeleteTerrain refers to a deleted or unknown terrain");
		}

		m_Terrains.release_slot(terrain.handle.index);
		++m_TerrainEpoch;
	}
}
