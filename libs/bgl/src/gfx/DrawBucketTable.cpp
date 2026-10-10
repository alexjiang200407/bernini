#include "gfx/DrawBucketTable.h"
#include "util/util.h"
#include <core/err/util.h>
#include <cstdint>
#include <optional>
#include <spdlog/spdlog.h>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		uint64_t
		PackKey(const GeometryStage geom, const MaterialType material, const LayerType layer)
		{
			return static_cast<uint64_t>(std::to_underlying(material)) |
			       (static_cast<uint64_t>(static_cast<uint32_t>(geom)) << 32u) |
			       (static_cast<uint64_t>(static_cast<uint32_t>(layer)) << 40u);
		}
	}

	GeometryStage
	GeometryStageOf(const GeomType geom)
	{
		switch (geom)
		{
		case GeomType::kStaticMesh:
			return GeometryStage::kStaticMesh;
		case GeomType::kSkinnedMesh:
			return GeometryStage::kSkinnedMesh;
		case GeomType::kInvalid:
		case GeomType::kCount:
			break;
		}
		core::fatal("A bucket's geometry kind is a drawable tier");
	}

	DrawBucketTable::DrawBucketTable(const uint32_t ceiling) : m_Ceiling(ceiling)
	{
		core::ensure(
			ceiling >= 2 && ceiling <= idl::cMaxDrawBuckets,
			"The bucket ceiling holds the fallback and the impostor bucket, and fits the cull "
			"chain's sizing");
		m_Flags.assign(ceiling, 0u);
		(void)Resolve(GeometryStage::kStaticMesh, MaterialType::kNull, LayerType::kOpaque);
		const uint32_t impostor =
			Resolve(GeometryStage::kImpostor, MaterialType::kPBR, LayerType::kOpaque);
		core::ensure(
			impostor == idl::cImpostorDrawBucket,
			"The impostor bucket is the second allocated");
	}

	uint32_t
	DrawBucketTable::Resolve(const GeometryStage geom, const MaterialType material, LayerType layer)
	{
		// Neither shades a base color, so there is no alpha for a coverage or blend layer to read.
		if (material == MaterialType::kNull || material == MaterialType::kAssert)
		{
			layer = LayerType::kOpaque;
		}

		if (material == MaterialType::kInvalid)
		{
			core::fatal("A bucket's material kind is a real one");
		}
		if (layer == LayerType::kInvalid || layer == LayerType::kCount)
		{
			core::fatal("A bucket's layer is a real one");
		}
		if (geom == GeometryStage::kGrass && layer != LayerType::kOpaque)
		{
			core::fatal("Grass is drawn opaque whatever its material's layer");
		}
		if (geom == GeometryStage::kTerrain && layer != LayerType::kOpaque)
		{
			core::fatal("A terrain takes an opaque material; CreateTerrain refuses the rest");
		}
		if (geom == GeometryStage::kImpostor &&
		    (material != MaterialType::kPBR || layer != LayerType::kOpaque))
		{
			core::fatal("The impostor stage has one bucket, opaque and lit through PBR");
		}
		if (geom == GeometryStage::kSkinnedMesh && material != MaterialType::kPBR &&
		    !GameSlot(material).has_value())
		{
			core::fatal("Skinned geometry is only drawable with a kPBR or a game surface material");
		}

		const uint64_t key = PackKey(geom, material, layer);
		if (const auto found = m_KeyToDrawBucket.find(key); found != m_KeyToDrawBucket.end())
		{
			return found->second;
		}

		if (m_Descs.size() >= m_Ceiling)
		{
			if (m_Refused.insert(key).second)
			{
				spdlog::error(
					"Draw bucket ceiling ({}) reached: (geom {}, material {}, layer {}) draws "
					"through the unlit fallback",
					m_Ceiling,
					static_cast<uint32_t>(geom),
					std::to_underlying(material),
					static_cast<uint32_t>(layer));
			}
			return 0u;
		}

		const auto bucket = static_cast<uint32_t>(m_Descs.size());
		m_Descs.push_back(DrawBucketDesc{ geom, material, layer });
		m_Flags[bucket] = FlagsOf(m_Descs.back());
		m_KeyToDrawBucket.emplace(key, bucket);

		return bucket;
	}

	uint32_t
	DrawBucketTable::Resolve(const GeometryStage geom, const MaterialHandle material)
	{
		const MaterialType type  = material.IsValid() ? material.materialType : MaterialType::kNull;
		const LayerType    layer = material.IsValid() ? material.layerType : LayerType::kOpaque;

		return Resolve(geom, type, layer);
	}

	const DrawBucketDesc&
	DrawBucketTable::Desc(const uint32_t bucket) const noexcept
	{
		core::ensure(bucket < Count(), "Desc takes an allocated bucket");
		return m_Descs[bucket];
	}

	bool
	DrawBucketTable::Transparent(const uint32_t bucket) const noexcept
	{
		core::ensure(bucket < Count(), "Transparent takes an allocated bucket");
		return HasFlag(bucket, idl::DrawBucketFlag::kTransparent);
	}

	void
	DrawBucketTable::SetSurfaceShading(std::vector<SurfaceShading> slots)
	{
		m_SurfaceShading = std::move(slots);
		for (uint32_t bucket = 0; bucket < Count(); ++bucket)
		{
			m_Flags[bucket] = FlagsOf(m_Descs[bucket]);
		}
	}

	bool
	DrawBucketTable::Water(const uint32_t bucket) const noexcept
	{
		core::ensure(bucket < Count(), "Water takes an allocated bucket");
		const std::optional<uint32_t> slot = GameSlot(m_Descs[bucket].material);
		return slot.has_value() && *slot < m_SurfaceShading.size() &&
		       m_SurfaceShading[*slot] == SurfaceShading::kWater;
	}

	bool
	DrawBucketTable::Occludee(const uint32_t bucket) const noexcept
	{
		core::ensure(bucket < Count(), "Occludee takes an allocated bucket");
		return HasFlag(bucket, idl::DrawBucketFlag::kOccludee);
	}

	uint32_t
	DrawBucketTable::FlagsOf(const DrawBucketDesc& desc) const noexcept
	{
		uint32_t flags = 0u;
		if (desc.layer == LayerType::kBlend)
		{
			flags |= std::to_underlying(idl::DrawBucketFlag::kTransparent);
		}
		const std::optional<uint32_t> slot  = GameSlot(desc.material);
		const bool                    water = slot.has_value() && *slot < m_SurfaceShading.size() &&
		                                      m_SurfaceShading[*slot] == SurfaceShading::kWater;
		if (desc.geom == GeometryStage::kStaticMesh && desc.layer != LayerType::kBlend && !water)
		{
			flags |= std::to_underlying(idl::DrawBucketFlag::kOccludee);
		}
		return flags;
	}

	bool
	DrawBucketTable::HasFlag(const uint32_t bucket, const idl::DrawBucketFlag flag) const noexcept
	{
		return (m_Flags[bucket] & std::to_underlying(flag)) != 0u;
	}
}
