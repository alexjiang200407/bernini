#pragma once
#include "idl/CullView.h"
#include "resource/FrameBuffer.h"
#include "resource/Sampler.h"
#include "types/EnvironmentMap.h"
#include "types/Viewport.h"
#include <bgl/SkyboxDesc.h>

namespace bgl
{
	class ISceneView;
	class CullState;

	/**
	 * The camera one draw is seen through, resolved: where it is this frame, where it was last
	 * frame, and the frustum derived from it.
	 *
	 * Distinct from ViewMatrices, which is the history record a SceneView keeps between frames.
	 * This is what a single draw resolved to, including its predecessor.
	 */
	struct ViewState
	{
		Viewport viewport;

		glm::mat4 viewProj{ 1.0f };
		glm::mat4 prevViewProj{ 1.0f };

		// Sub-pixel offsets baked into the projections above. Zero without temporal AA.
		glm::vec2 jitter{ 0.0f };
		glm::vec2 prevJitter{ 0.0f };

		glm::vec3 cameraPos{ 0.0f };

		// The frustum planes the cull dispatch tests against, derived from viewProj.
		idl::CullView cullView{};

		// Decorrelates the hashed-alpha pattern between frames. Zero without temporal AA, where a
		// pattern that changed every frame would be flicker rather than coverage.
		float alphaHashSeed = 0.0f;
	};

	/** The attachments a draw renders into, all owned by the frame's render target. */
	struct DrawTargets
	{
		RtvHandle sceneColor;
		RtvHandle motionVector;
		DsvHandle depth;
	};

	/** What a draw shades against: the image-based environment, and the sky behind it. */
	struct DrawLighting
	{
		EnvironmentMap env;
		float          exposure = 1.0f;

		std::optional<SkyboxDesc> skybox;
		glm::mat4                 skyboxClipToWorld{ 1.0f };
		glm::mat4                 skyboxPrevWorldToClip{ 1.0f };
	};

	/** The scene's standard samplers, resolved once per draw so a pass need not reach for them. */
	struct DrawSamplers
	{
		SamplerHandle anisoLinearWrap;
		SamplerHandle linearClamp;
	};

	/**
	 * Everything one RenderJob resolves to, handed to every pass that records for it.
	 */
	struct DrawData
	{
		uint32_t                    drawIdx = 0;
		core::SharedRef<ISceneView> view    = nullptr;

		// Which of the SceneView's frustums this records for. An identifier only -- the scratch
		// itself arrives through cullState below, and the count lives on the view
		// (SceneView::GetCullStateCount). The cull pipeline's pass names are keyed on it as well as
		// drawIdx, since a draw may record that pipeline once per frustum.
		uint32_t cullIdx = 0;

		// This draw's frustum scratch, owned by the view. Non-owning: a DrawData is captured by
		// value into the pass exec lambdas, so the SharedRef above travels with the pointer and the
		// view cannot be destroyed while a pass still holds one. The view staying alive is not
		// enough on its own -- see SceneView::EnsureCullStateCount for what invalidates the address.
		CullState* cullState = nullptr;

		ViewState    viewState;
		DrawTargets  targets;
		DrawLighting lighting;
		DrawSamplers samplers;
	};
}
