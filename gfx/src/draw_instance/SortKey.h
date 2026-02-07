#pragma once
#include "draw_instance/GeometryType.h"
#include "draw_instance/MaterialType.h"

namespace gfx
{
	struct SortKey
	{
		enum class Layer : uint8_t
		{
			kBackground  = 0,
			kOpaque      = 1,
			kAlphaTest   = 2,
			kTransparent = 3
		};

		SortKey() = default;
		SortKey(
			Layer        layerID,
			GeometryType geomType,
			MaterialType matType,
			float        depthViewSpace) noexcept;

		SortKey&
		operator=(uint64_t val) noexcept
		{
			m_value = val;
			return *this;
		};

		[[nodiscard]] GeometryType
		GetGeomType() const noexcept
		{
			return static_cast<GeometryType>((m_value >> 56) & 0x3F);
		}

		[[nodiscard]] MaterialType
		GetMaterialType() const noexcept
		{
			return static_cast<MaterialType>((m_value >> 46) & 0x3FF);
		}

	private:
		uint64_t m_value = 0;
	};

	static_assert(sizeof(SortKey) == 8);

}
