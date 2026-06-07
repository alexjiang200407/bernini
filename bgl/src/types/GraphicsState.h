#pragma once
#include "pipeline/GraphicsPipeline.h"
#include "resource/FrameBuffer.h"
#include "types/ViewportState.h"

namespace bgl
{
	struct GraphicsState
	{
		GraphicsPipelineHandle pipeline;
		ViewportState          viewportState;
		FrameBuffer            frameBuffer;
		void*                  rootConstantData;
		size_t                 rootConstantSize;
	};
}
