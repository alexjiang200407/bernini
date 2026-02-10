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
}
