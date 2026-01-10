#pragma once
#include <fg/FrameGraphResource.hpp>

namespace gfx
{
	struct FrameData
	{
		//FrameGraphResource drawIndirectBuffer;
		//FrameGraphResource drawIndirectCountBuffer;
		FrameGraphResource frameConstants;
		FrameGraphResource drawIndirectArgs;
		FrameGraphResource drawIndirectCount;
		FrameGraphResource visibleInstanceCount;
		FrameGraphResource visibleInstanceBuffer;
		FrameGraphResource indexBuffer;
		FrameGraphResource vertexBuffer;

		FrameGraphResource frameConstantsBindingSet;
		FrameGraphResource frameConstantsBindingLayout;
	};
}
