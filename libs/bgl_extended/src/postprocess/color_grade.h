#pragma once

#include <bgl/IRenderTarget.h>

namespace bgl
{
	/** @throws GraphicsError naming the first field outside the range IRenderTarget documents. */
	void
	ValidateColorGradeSettings(const ColorGradeSettings& settings);
}
