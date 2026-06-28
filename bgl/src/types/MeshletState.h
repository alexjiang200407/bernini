#pragma once
#include "resource/FrameBuffer.h"
#include "types/ViewportState.h"

namespace bgl
{
	struct MeshletKernel;

	struct MeshletState
	{
		const MeshletKernel* kernel = nullptr;
		ViewportState        viewportState;
		FrameBuffer          frameBuffer;
	};
}
