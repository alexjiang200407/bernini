#pragma once

namespace bgl
{
	enum class LayerType : uint8_t
	{
		kInvalid = static_cast<uint8_t>(-1),
		kOpaque  = 0,
		kAlphaTest,
		kTransparent,
		kCount,
	};
}
