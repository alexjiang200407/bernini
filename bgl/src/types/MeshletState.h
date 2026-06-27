#pragma once
#include "resource/FrameBuffer.h"
#include "types/ViewportState.h"

namespace bgl
{
	class IMeshletPipeline;
	class Uniforms;

	struct MeshletState
	{
		core::SharedRef<IMeshletPipeline> pipeline;
		ViewportState                     viewportState;
		FrameBuffer                       frameBuffer;
		Uniforms*                         uniforms = nullptr;
	};
}
