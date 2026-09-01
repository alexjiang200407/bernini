#pragma once
#include "types/Viewport.h"

namespace bgl
{
	struct Rect
	{
		int minX = 0, maxX = 0;
		int minY = 0, maxY = 0;

		Rect() noexcept = default;
		explicit Rect(const Viewport& viewport) noexcept :
			minX(int(floorf(viewport.minX))), maxX(int(ceilf(viewport.maxX))),
			minY(int(floorf(viewport.minY))), maxY(int(ceilf(viewport.maxY)))
		{}
	};
}
