#pragma once
#include "pipeline/MeshletPipeline.h"
#include "resource/FrameBuffer.h"
#include "types/ViewportState.h"

namespace bgl
{
	class Uniforms;

	struct MeshletState
	{
		MeshletPipelineHandle pipeline;
		ViewportState         viewportState;
		FrameBuffer           frameBuffer;
		Uniforms*             uniforms = nullptr;
	};
}
