#pragma once

namespace gfx
{
	enum class GeometryType : uint8_t
	{
		kInvalid = 0xFF,
		kStatic  = 0,
		kSkinned,
		kTerrain,
		kParticle,
		kSpline,
		kCount,
	};

	constexpr std::string_view
	getGeomTypeName(GeometryType geom)
	{
		switch (geom)
		{
		case GeometryType::kStatic:
			return "Static"sv;
		case GeometryType::kSkinned:
			return "Skinned"sv;
		case GeometryType::kTerrain:
			return "Terrain"sv;
		default:
			return "Invalid"sv;
		}
	}
}
