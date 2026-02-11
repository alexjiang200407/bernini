#include "types/SortKey.h"

namespace gfx
{
	// Bit layout:
	// Layer:         bits [62-63] (2 bits)
	// PSO IDX:       bits [48-61] (14 bits) - UNIQUE PSO identifier (your enum)
	// Material type: bits [40-47] (8 bits)  - metadata for secondary sorting
	// Geom type:     bits [32-39] (8 bits)  - metadata for secondary sorting
	// Depth:         bits [00-31] (32 bits)

	SortKey::SortKey(LayerType layerID, PSO pso, float depthViewSpace) noexcept
	{
		union
		{
			float    f;
			uint32_t u;
		} depthCast;
		depthCast.f       = depthViewSpace;
		uint32_t depthInt = depthCast.u;

		auto geomType = psoGeomType(pso);
		auto matType  = psoMaterialType(pso);
		depthInt ^= (-(int32_t)(depthInt >> 31) | 0x80000000);

		uint64_t layerVal = static_cast<uint64_t>(layerID) & 0x3;    // 2 bits
		uint64_t psoVal   = static_cast<uint64_t>(pso) & 0x3FFF;     // 14 bits
		uint64_t matVal   = static_cast<uint64_t>(matType) & 0xFF;   // 8 bits
		uint64_t geomVal  = static_cast<uint64_t>(geomType) & 0xFF;  // 8 bits
		uint64_t depthVal = static_cast<uint64_t>(depthInt);         // 32 bits

		m_value = (layerVal << 62) | (psoVal << 48) | (matVal << 40) | (geomVal << 32) | depthVal;
	}

	[[nodiscard]]
	PSO
	SortKey::UpdatePSO(MaterialType matType) noexcept
	{
		auto geomType  = GetGeomType();
		auto layerType = GetLayerType();
		auto newPSO    = computePSO(layerType, geomType, matType);

		if (newPSO != PSO::kInvalid)
			SetPSO(newPSO);

		return newPSO;
	}

	void
	SortKey::SetPSO(PSO pso) noexcept
	{
		auto psoVal = static_cast<uint64_t>(pso) & 0x3FFF;
		m_value &= ~(0x3FFFULL << 48);
		m_value |= (psoVal << 48);
	}

	void
	SortKey::SetMaterialType(MaterialType matType) noexcept
	{
		auto matVal = static_cast<uint64_t>(matType) & 0xFF;
		m_value &= ~(0xFFULL << 40);
		m_value |= (matVal << 40);
	}

	void
	SortKey::SetLayerType(LayerType layerID) noexcept
	{
		auto layerVal = static_cast<uint64_t>(layerID) & 0x3;
		m_value &= ~(0x3ULL << 62);
		m_value |= (layerVal << 62);
	}
}
