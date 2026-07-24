#pragma once
#include "idl/CullView.h"
#include "resource/FrameBuffer.h"
#include "resource/Sampler.h"
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
		glm::mat4                   prevViewProj{ 1.0f };
		idl::CullView               cullView{};
		glm::vec3                   cameraPos{ 0.0f };
		RtvHandle                   backBufferHandle;
		DsvHandle                   depthBufferHandle;
		RtvHandle                   motionVectorHandle;
		std::string                 backBufferName;
		std::string                 depthBufferName;
		std::string                 motionVectorName;
		SamplerHandle               anisoLinearWrapSampler;
		SamplerHandle               linearClampSampler;
		EnvironmentMap              env;
		float                       exposure = 1.0f;

		std::optional<SkyboxDesc> skybox;
		glm::mat4                 skyboxClipToWorld{ 1.0f };
	};
}
