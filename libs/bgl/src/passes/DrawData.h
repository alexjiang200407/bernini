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
	class ViewCullState;

	struct DrawData
	{
		uint32_t                    drawIdx = 0;
		core::SharedRef<ISceneView> view    = nullptr;

		// This draw's frustum scratch, owned by the view. Non-owning; valid for the frame only.
		ViewCullState* cullState = nullptr;
		Viewport       viewport;
		glm::mat4      viewProj{ 1.0f };
		glm::mat4      prevViewProj{ 1.0f };
		glm::vec2      jitter{ 0.0f };
		glm::vec2      prevJitter{ 0.0f };
		idl::CullView  cullView{};
		glm::vec3      cameraPos{ 0.0f };
		RtvHandle      sceneColorHandle;
		DsvHandle      depthBufferHandle;
		RtvHandle      motionVectorHandle;
		SamplerHandle  anisoLinearWrapSampler;
		SamplerHandle  linearClampSampler;
		EnvironmentMap env;
		float          exposure = 1.0f;

		// Decorrelates the hashed-alpha pattern between frames. Zero without temporal AA, where a
		// pattern that changed every frame would be flicker rather than coverage.
		float alphaHashSeed = 0.0f;

		std::optional<SkyboxDesc> skybox;
		glm::mat4                 skyboxClipToWorld{ 1.0f };
		glm::mat4                 skyboxPrevWorldToClip{ 1.0f };
	};
}
