#pragma once

namespace bgl
{
	enum class MaterialType : uint8_t
	{
		kInvalid = static_cast<uint8_t>(-1),
		kPBR     = 0,
		kCount,
	};
}
