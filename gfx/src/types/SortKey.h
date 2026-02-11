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
			uint64_t geomVal = (m_value >> 32) & 0xFF;
			return static_cast<GeometryType>(geomVal);
		}

		[[nodiscard]] LayerType
		GetLayerType() const noexcept
		{
			uint64_t layerVal = (m_value >> 62) & 0x3;
			return static_cast<LayerType>(layerVal);
		}

		[[nodiscard]] MaterialType
		GetMaterialType() const noexcept
		{
			uint64_t matVal = (m_value >> 40) & 0xFF;
			return static_cast<MaterialType>(matVal);
		}

		[[nodiscard]] PSO
		GetPSO() const noexcept
		{
			return static_cast<PSO>((m_value >> 46) & 0x3FF);
		}

		PSO
		UpdatePSO(MaterialType matType) noexcept;

		void
		SetPSO(PSO pso) noexcept;

		void
		SetMaterialType(MaterialType matType) noexcept;

		void
		SetLayerType(LayerType matType) noexcept;

	private:
		uint64_t m_value = 0;
	};

	static_assert(sizeof(SortKey) == 8);

}
