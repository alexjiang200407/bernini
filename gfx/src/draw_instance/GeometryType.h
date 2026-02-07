#pragma once

namespace gfx
{
	enum class GeometryType : uint8_t
	{
		kStatic = 0,
		kSkinned,
		kTerrain,
		kParticle,
		kSpline,
		kCount
	};

}
