#pragma once
#include "pipeline/MeshletKernel.h"
#include "resource/Rtv.h"
#include "resource/Sampler.h"
#include "resource/Srv.h"
#include "types/ViewportState.h"

namespace bgl
{
	class IDevice;
	class FrameGraph;
	class PassContext;

	/**
	 * Turns the linear HDR scene colour into the displayed image: the frame's last colour pass, and
	 * the only writer of the backbuffer -- which is what keeps the capture path, a readback of the
	 * last presented backbuffer, describing what was shown.
	 *
	 * Today that is the display curve alone. Everything between a resolved scene and the screen
	 * belongs here as it lands -- bloom, grading, exposure adaptation -- so the stage is named for
	 * the role rather than for its one current step.
	 *
	 * Exposure is not applied here: it is a per-view scale the geometry passes have already folded
	 * in, while a target may carry several views.
	 */
	class PostProcessPass
	{
	public:
		struct Args
		{
			// The last HDR stage's output: scene colour directly, or the freshly resolved history
			// when the target has TAA on.
			SrvHandle     source;
			std::string   sourceName;
			RtvHandle     backBuffer;
			SamplerHandle sampler;
			Viewport      viewport;

			// Set only when an outline-mask pass ran this frame; the shader samples the mask
			// behind the flag, so a disabled frame binds nothing.
			SrvHandle outlineMask;
			glm::vec2 maskTexelSize{ 0.0f };
			bool      outlineEnabled = false;
		};

		PostProcessPass() = default;
		~PostProcessPass() noexcept { logger::trace("~PostProcessPass"); }

		PostProcessPass(const PostProcessPass&) noexcept = delete;
		PostProcessPass(PostProcessPass&&) noexcept      = delete;

		PostProcessPass&
		operator=(const PostProcessPass&) noexcept = delete;

		PostProcessPass&
		operator=(PostProcessPass&&) noexcept = delete;

		void
		Release()
		{
			m_Kernel.Reset();
		}

		void
		Init(IDevice* device);

		void
		AttachToFrameGraph(FrameGraph& fg, const Args& args);

	private:
		void
		Execute(const Args& args, const PassContext& resources);

		MeshletKernel m_Kernel;
	};
}
