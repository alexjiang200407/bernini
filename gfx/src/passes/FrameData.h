#pragma once
#include <fg/FrameGraphResource.hpp>

namespace gfx
{
	struct FrameData
	{
		FrameGraphResource frameConstants;
		FrameGraphResource drawIndirectBuffer;
		FrameGraphResource drawIndirectCountBuffer;
		FrameGraphResource indexBuffer;
		FrameGraphResource vertexBuffer;

		FrameGraphResource frameConstantsBindingSet;
		FrameGraphResource frameConstantsBindingLayout;
	};
}
