#pragma once
#include "resource/FrameBuffer.h"
#include "types/ViewportState.h"

namespace bgl
{
	struct GraphicsKernel;

	struct GraphicsState
	{
		const GraphicsKernel* kernel = nullptr;
		ViewportState         viewportState;
		FrameBuffer           frameBuffer;
	};
}
