#pragma once

#include <bgl/types/PostProcess.h>

namespace bgl
{
	/** @throws GraphicsError naming the first field outside the range IRenderTarget documents. */
	void
	ValidatePostProcess(const PostProcess& postProcess);
}
