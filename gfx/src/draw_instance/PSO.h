#pragma once
#include "draw_instance/GeometryType.h"
#include "draw_instance/LayerType.h"
#include "draw_instance/MaterialType.h"

namespace gfx
{
	// Needs to reflect the order PSOs are run
	enum class PSO : uint16_t
	{
		kInvalid                    = 0xFFFF,
		kTransparent_StaticMesh_PBR = 0,
		kAlphaTest_StaticMesh_PBR,
		kOpaque_StaticMesh_PBR,
		kCount,
	};

	constexpr auto PSO_COUNT = static_cast<uint16_t>(PSO::kCount);

	constexpr GeometryType
	psoGeomType(PSO pso)
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
	psoMaterialType(PSO pso)
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

	constexpr LayerType
	psoLayerType(PSO pso)
	{
		switch (pso)
		{
		case PSO::kOpaque_StaticMesh_PBR:
			return LayerType::kOpaque;
		case PSO::kAlphaTest_StaticMesh_PBR:
			return LayerType::kAlphaTest;
		case PSO::kTransparent_StaticMesh_PBR:
			return LayerType::kTransparent;
		}
		return LayerType::kInvalid;
	}
}
