#pragma once
#include "resource/FrameBuffer.h"
#include "resource/Sampler.h"
#include "scene/transparent_sort.h"
#include "types/EnvironmentMap.h"
#include "types/ViewportState.h"
#include <bgl/SkyboxDesc.h>

namespace bgl
{
	class ISceneView;

	struct DrawData
	{
		uint32_t                    drawIdx = 0;
		core::SharedRef<ISceneView> view    = nullptr;
		Viewport                    viewport;
		glm::mat4                   viewProj{ 1.0f };
		glm::vec3                   cameraPos{ 0.0f };
		RtvHandle                   backBufferHandle;
		DsvHandle                   depthBufferHandle;
		std::string                 backBufferName;
		std::string                 depthBufferName;
		SamplerHandle               anisoLinearWrapSampler;
		SamplerHandle               linearClampSampler;
		EnvironmentMap              env;
		float                       exposure = 1.0f;

		std::optional<SkyboxDesc> skybox;
		glm::mat4                 skyboxClipToWorld{ 1.0f };

		// Depth-sorted transparent draw runs, back-to-front, built CPU-side for this view/camera.
		std::vector<TransparentRun> transparentRuns;
	};
}
