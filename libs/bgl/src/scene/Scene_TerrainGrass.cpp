#include "scene/Scene.h"
#include "scene/terrain_grass_window.h"
#include <algorithm>
#include <bgl/IScene.h>
#include <bgl/idl/Constants.h>
#include <bgl/idl/TerrainGrass.h>
#include <bgl/types/GrassHandle.h>
#include <bgl/types/TerrainGrassDesc.h>
#include <bgl/types/TerrainHandle.h>
#include <cmath>
#include <core/containers/slot_handle.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numbers>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		// One tile is one chunk of the grass stage.
		static_assert(
			idl::cTerrainGrassClumpsAcross * idl::cTerrainGrassClumpsAcross ==
			idl::cGrassClumpsPerChunk);

		constexpr float c_Vertical = std::numbers::pi_v<float> * 0.5f;

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

		void
		ValidateLayer(const TerrainGrassDesc& layer, const size_t index)
		{
			const auto refuse = [index](const std::string_view why) {
				throw SceneError(std::format("AttachTerrainGrass: layer {}: {}", index, why));
			};

			if (!IsPositive(layer.spacing) || !IsPositive(layer.patchSize))
			{
				refuse("spacing and patchSize must be finite and positive");
			}
			if (!std::isfinite(layer.maxSlope) || layer.maxSlope < 0.0f ||
			    layer.maxSlope > c_Vertical || !IsNonNegative(layer.slopeBlend))
			{
				refuse("maxSlope must be in [0, pi/2] and slopeBlend finite and non-negative");
			}
			if (!std::isfinite(layer.minHeight) || !std::isfinite(layer.maxHeight) ||
			    layer.minHeight > layer.maxHeight || !IsNonNegative(layer.heightBlend))
			{
				refuse(
					"heights must be finite with minHeight <= maxHeight, and heightBlend finite "
					"and non-negative");
			}
			if (!std::isfinite(layer.patchCoverage) || layer.patchCoverage < 0.0f ||
			    layer.patchCoverage > 1.0f)
			{
				refuse("patchCoverage must be in [0, 1]");
			}
		}

		[[nodiscard]] idl::TerrainGrass
		BuildLayer(
			const TerrainGrassDesc& layer,
			const TerrainMeta&      terrain,
			const uint32_t          seed) noexcept
		{
			auto record       = idl::TerrainGrass();
			record.terrain    = terrain.record;
			record.nodeBounds = terrain.nodeBounds;
			record.tileSize   = layer.spacing * static_cast<float>(idl::cTerrainGrassClumpsAcross);
			record.seed       = seed;
			record.fullSlopeCos = std::cos(layer.maxSlope);
			record.noneSlopeCos = std::cos(std::min(layer.maxSlope + layer.slopeBlend, c_Vertical));
			record.minHeight    = layer.minHeight;
			record.maxHeight    = layer.maxHeight;
			record.heightBlend  = layer.heightBlend;
			record.patchSize    = layer.patchSize;
			record.patchCoverage = layer.patchCoverage;
			return record;
		}
	}

	void
	Scene::AttachTerrainGrass(
		const TerrainHandle                     terrain,
		const std::span<const TerrainGrassDesc> layers)
	{
		if (!IsTerrainAlive(terrain))
		{
			throw SceneError("AttachTerrainGrass: the terrain is deleted or unknown");
		}

		for (size_t i = 0; i < layers.size(); ++i)
		{
			if (!IsGrassAlive(layers[i].look))
			{
				throw SceneError(
					std::format("AttachTerrainGrass: layer {}: the look is null or deleted", i));
			}
			ValidateLayer(layers[i], i);

			const float tileSize =
				layers[i].spacing * static_cast<float>(idl::cTerrainGrassClumpsAcross);
			const float fadeEnd = m_Grass[layers[i].look.handle.index].desc.density.fadeEnd;
			if (!(TerrainGrassWindowTiles(fadeEnd, tileSize) <=
			      static_cast<float>(c_MaxTerrainGrassWindowTiles)))
			{
				throw SceneError(
					std::format(
						"AttachTerrainGrass: layer {}: a spacing of {} grows the look's {} fade in "
						"a window wider than {} tiles",
						i,
						layers[i].spacing,
						fadeEnd,
						c_MaxTerrainGrassWindowTiles));
			}
		}

		TerrainMeta& meta    = m_Terrains[terrain.handle.index];
		auto         records = std::vector<TerrainGrassRecord>();
		records.reserve(layers.size());
		try
		{
			for (size_t i = 0; i < layers.size(); ++i)
			{
				const idl::TerrainGrass record =
					BuildLayer(layers[i], meta, static_cast<uint32_t>(i));
				records.push_back(
					TerrainGrassRecord{
						.look     = layers[i].look,
						.entry    = m_TerrainGrass.Add(record),
						.tileSize = record.tileSize,
					});
			}
		}
		catch (...)
		{
			for (const TerrainGrassRecord& record : records)
			{
				m_TerrainGrass.Erase(record.entry);
			}
			throw;
		}

		ReleaseTerrainGrass(meta.grass);
		for (const TerrainGrassRecord& record : records)
		{
			++m_Grass[record.look.handle.index].useCount;
		}
		meta.grass = std::move(records);
		++m_GrassEpoch;
	}

	void
	Scene::ReleaseTerrainGrass(std::vector<TerrainGrassRecord>& layers) noexcept
	{
		for (const TerrainGrassRecord& layer : layers)
		{
			core::ensure(
				IsGrassAlive(layer.look),
				"a live terrain binds a grass look that is already gone");
			if (IsGrassAlive(layer.look) && m_Grass[layer.look.handle.index].useCount > 0)
			{
				--m_Grass[layer.look.handle.index].useCount;
			}
			m_TerrainGrass.Erase(layer.entry);
		}

		if (!layers.empty())
		{
			layers.clear();
			++m_GrassEpoch;
		}
	}
}
