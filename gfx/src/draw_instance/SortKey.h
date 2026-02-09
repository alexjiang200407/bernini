#pragma once
#include "draw_instance/GeometryType.h"
#include "draw_instance/LayerType.h"
#include "draw_instance/MaterialType.h"
#include "draw_instance/PSO.h"

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
