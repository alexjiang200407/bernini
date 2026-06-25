#pragma once

namespace bgl
{
	enum class MaterialType : uint8_t
	{
		kInvalid = static_cast<uint8_t>(-1),
		kNull    = 0,  // unlit white color, no textures, no lighting.
		kPBR,
		kCount,
	};
}
