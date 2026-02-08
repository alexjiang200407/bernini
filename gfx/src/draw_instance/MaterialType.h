#pragma once

namespace gfx
{
	enum class MaterialType : uint16_t
	{
		kInvalid = 0xFFFF,
		kPBR     = 0,
		kSolidColor,
		kCount,
	};
}
