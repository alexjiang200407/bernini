#pragma once
#include "draw_instance/GeometryType.h"
#include "draw_instance/MaterialType.h"

namespace gfx
{
	enum class PSO : uint16_t
	{
		kInvalid               = 0xFFFF,
		kOpaque_StaticMesh_PBR = 0,
		kAlphaTest_StaticMesh_PBR,
		kTransparent_StaticMesh_PBR,
		kCount,
	};

	constexpr auto PSO_COUNT = static_cast<uint16_t>(PSO::kCount);

	constexpr GeometryType
	pso2GeomType(PSO pso)
	{
		switch (pso)
		{
		case PSO::kOpaque_StaticMesh_PBR:
		case PSO::kAlphaTest_StaticMesh_PBR:
		case PSO::kTransparent_StaticMesh_PBR:
			return GeometryType::kStatic;
		}

		return GeometryType::kInvalid;
	}

	constexpr MaterialType
	pso2MaterialType(PSO pso)
	{
		switch (pso)
		{
		case PSO::kOpaque_StaticMesh_PBR:
		case PSO::kAlphaTest_StaticMesh_PBR:
		case PSO::kTransparent_StaticMesh_PBR:
			return MaterialType::kPBR;
		}

		return MaterialType::kInvalid;
	}
}
