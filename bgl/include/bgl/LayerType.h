#pragma once

namespace bgl
{
	enum class LayerType : uint8_t
	{
		kOpaque = 0,
		kAlphaTest,
		kTransparent,
		kCount,
	};
}
