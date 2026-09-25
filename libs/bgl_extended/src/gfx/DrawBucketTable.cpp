#include "gfx/DrawBucketTable.h"
#include "util/util.h"
#include <bgl_common/gassert.h>
#include <cstdint>
#include <spdlog/spdlog.h>

namespace bgl
{
	namespace
	{
		uint64_t
		PackKey(const GeometryStage geom, const MaterialType material, const LayerType layer)
		{
			return static_cast<uint64_t>(static_cast<uint32_t>(material)) |
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
		gfatal("A bucket's geometry kind is a drawable tier");
	}

	DrawBucketTable::DrawBucketTable(const uint32_t ceiling) : m_Ceiling(ceiling)
	{
		gassert(
			ceiling >= 1 && ceiling <= idl::cMaxDrawBuckets,
			"The bucket ceiling holds the fallback and fits the cull chain's sizing");
		m_Flags.assign(ceiling, 0u);
		(void)Resolve(GeometryStage::kStaticMesh, MaterialType::kNull, LayerType::kOpaque);
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
			gfatal("A bucket's material kind is a real one");
		}
		if (layer == LayerType::kInvalid || layer == LayerType::kCount)
		{
			gfatal("A bucket's layer is a real one");
		}
		if (geom == GeometryStage::kGrass && layer != LayerType::kOpaque)
		{
			gfatal("Grass is drawn opaque whatever its material's layer");
		}
		if (geom == GeometryStage::kSkinnedMesh && material != MaterialType::kPBR &&
		    !GameSlot(material).has_value())
		{
			gfatal("Skinned geometry is only drawable with a kPBR or a game surface material");
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
				logger::error(
					"Draw bucket ceiling ({}) reached: (geom {}, material {}, layer {}) draws "
					"through the unlit fallback",
					m_Ceiling,
					static_cast<uint32_t>(geom),
					static_cast<uint32_t>(material),
					static_cast<uint32_t>(layer));
			}
			return 0u;
		}

		const auto bucket = static_cast<uint32_t>(m_Descs.size());
		m_Descs.push_back(DrawBucketDesc{ geom, material, layer });
		m_Flags[bucket] = layer == LayerType::kBlend ?
		                      static_cast<uint32_t>(idl::DrawBucketFlag::kTransparent) :
		                      0u;
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
		gassert(bucket < Count(), "Desc takes an allocated bucket");
		return m_Descs[bucket];
	}

	bool
	DrawBucketTable::Transparent(const uint32_t bucket) const noexcept
	{
		gassert(bucket < Count(), "Transparent takes an allocated bucket");
		return (m_Flags[bucket] & static_cast<uint32_t>(idl::DrawBucketFlag::kTransparent)) != 0u;
	}
}
