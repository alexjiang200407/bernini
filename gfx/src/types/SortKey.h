#pragma once
#include "types/GeometryType.h"
#include "types/LayerType.h"
#include "types/MaterialType.h"
#include "types/PSO.h"

namespace gfx
{
	struct SortKey
	{
		SortKey() = default;
		SortKey(LayerType layerID, PSO pso, float depthViewSpace) noexcept;

		SortKey&
		operator=(uint64_t val) noexcept
		{
			m_value = val;
			return *this;
		};

		[[nodiscard]] GeometryType
		GetGeomType() const noexcept
		{
			return psoGeomType(GetPSO());
		}

		[[nodiscard]] PSO
		GetPSO() const noexcept
		{
			return static_cast<PSO>((m_value >> 46) & 0x3FF);
		}

		[[nodiscard]] MaterialType
		GetMaterialType() const noexcept
		{
			return psoMaterialType(GetPSO());
		}

	private:
		uint64_t m_value = 0;
	};

	static_assert(sizeof(SortKey) == 8);

}
