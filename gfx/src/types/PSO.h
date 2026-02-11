#pragma once
#include "types/GeometryType.h"
#include "types/LayerType.h"
#include "types/MaterialType.h"

namespace gfx
{
	// Needs to be the order PSOs are run. e.g. Opaque first
	enum class PSO : uint16_t
	{
		kInvalid           = 0xFFFF,
		kOpaque_Static_PBR = 0,
		kAlphaTest_Static_PBR,
		kTransparent_Static_PBR,
		kCount,
	};

	constexpr auto PSO_COUNT = static_cast<uint16_t>(PSO::kCount);

	inline constexpr GeometryType
	psoGeomType(PSO pso)
	{
		switch (pso)
		{
		case PSO::kOpaque_Static_PBR:
		case PSO::kAlphaTest_Static_PBR:
		case PSO::kTransparent_Static_PBR:
			return GeometryType::kStatic;
		}

		return GeometryType::kInvalid;
	}

	inline constexpr LayerType
	psoLayerType(PSO pso)
	{
		switch (pso)
		{
		case PSO::kOpaque_Static_PBR:
			return LayerType::kOpaque;
		case PSO::kAlphaTest_Static_PBR:
			return LayerType::kAlphaTest;
		case PSO::kTransparent_Static_PBR:
			return LayerType::kTransparent;
		}

		return LayerType::kInvalid;
	}

	inline constexpr MaterialType
	psoMaterialType(PSO pso)
	{
		switch (pso)
		{
		case PSO::kOpaque_Static_PBR:
		case PSO::kAlphaTest_Static_PBR:
		case PSO::kTransparent_Static_PBR:
			return MaterialType::kPBR;
		}

		return MaterialType::kInvalid;
	}

	PSO
	computePSO(LayerType layer, GeometryType geomType, MaterialType matType);

}
