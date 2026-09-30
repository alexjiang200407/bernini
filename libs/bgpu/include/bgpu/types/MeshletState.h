#pragma once
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/FrameBuffer.h>
#include <bgpu/types/ViewportState.h>

namespace bgl
{
	struct MeshletKernel;

	struct MeshletState
	{
		const MeshletKernel* kernel = nullptr;
		ViewportState        viewportState;
		FrameBuffer          frameBuffer;
		BufferHandle         indirectArgs;
		BufferHandle         commandCounts;
	};
}
