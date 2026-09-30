#pragma once

#include <cstdint>
namespace bgl
{
	enum class LayerType : uint8_t
	{
		kInvalid = static_cast<uint8_t>(-1),
		kOpaque  = 0,
		kMask,
		kBlend,
		kHashed,
		kCount,
	};
}
