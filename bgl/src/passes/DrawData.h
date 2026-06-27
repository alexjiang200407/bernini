#pragma once
#include "resource/FrameBuffer.h"
#include "types/ViewportState.h"

namespace bgl
{
	class IScene;

	struct DrawData
	{
		uint32_t                drawIdx = 0;
		core::SharedRef<IScene> scene   = nullptr;
		Viewport                viewport;
		glm::mat4               viewProj{ 1.0f };
		RtvHandle               backBufferHandle;
		DsvHandle               depthBufferHandle;
		std::string             backBufferName;
		std::string             depthBufferName;
	};
}
