#pragma once

namespace gfx
{
	enum class MaterialType : uint16_t
	{
		kOpaque = 0,
		kAlphaTest,
		kTransparent,
		kCount
	};
}
