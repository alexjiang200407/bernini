#pragma once

namespace gfx
{
	enum class LayerType : uint8_t
	{
		kInvalid     = 0xFF,
		kBackground  = 0,
		kOpaque      = 1,
		kAlphaTest   = 2,
		kTransparent = 3
	};
}
