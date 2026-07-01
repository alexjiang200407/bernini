#pragma once
#include "resource/FrameBuffer.h"
#include "types/ViewportState.h"

namespace bgl
{
	class ISceneView;

	struct DrawData
	{
		uint32_t                    drawIdx = 0;
		core::SharedRef<ISceneView> view    = nullptr;
		Viewport                    viewport;
		glm::mat4                   viewProj{ 1.0f };
		RtvHandle                   backBufferHandle;
		DsvHandle                   depthBufferHandle;
		std::string                 backBufferName;
		std::string                 depthBufferName;
	};
}
