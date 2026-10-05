#pragma once
#include "passes/PassInitContext.h"
#include <bgl/types/PostProcess.h>
#include <bgl/types/Viewport.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/types/ViewportState.h>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <string>

namespace bgpu
{
	class PipelineBatch;
	class IDevice;
}

namespace bgl
{
	class FrameGraph;
	class PassContext;

	/**
	 * Turns the linear HDR scene colour into the displayed image: the frame's last colour pass over
	 * the scene, and the first writer of the backbuffer, which it covers whole. Only the overlay
	 * writes it afterwards, blending over this -- so the capture path, a readback of the last
	 * presented backbuffer, still describes what was shown.
	 *
	 * Today that is the colour split, the bloom combine, the colour grade, the display curve and
	 * film grain. Everything between a
	 * resolved scene and the screen belongs here as it lands -- exposure adaptation next -- so the
	 * stage is named for the role rather than for its current steps.
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
			bgpu::SrvHandle source;
			std::string     sourceName;
			bgpu::RtvHandle backBuffer;

			// Point where the source is already on the backbuffer's grid, which is every frame the
			// resolve ran and every unscaled one; linear is what carries a render-resolution scene
			// colour across when it did not, and what the colour split reads between texels with.
			bgpu::SamplerHandle sampler;
			bgpu::Viewport      viewport;

			// Set only when an outline-mask pass ran this frame; the shader samples the mask
			// behind the flag, so a disabled frame binds nothing. The mask is on the render grid
			// and its dilate is a coverage test, so it is point-sampled whatever the source is.
			bgpu::SrvHandle     outlineMask;
			bgpu::SamplerHandle maskSampler;
			glm::vec2           maskSize{ 0.0f };
			bool                outlineEnabled = false;

			// The display curve's LUT, and the linear clamp it is read through.
			bgpu::SrvHandle     tonemapLut;
			bgpu::SamplerHandle lutSampler;

			// Set only when the bloom passes ran this frame, which a post-process with bloom on
			// can still skip when a pool refused the chain; the shader samples the chain behind the
			// flag, so a frame without it binds nothing. Half the source's resolution, so it is
			// always linearly sampled.
			bgpu::SrvHandle     bloom;
			bgpu::SamplerHandle bloomSampler;
			std::string         bloomName;
			bool                bloomRan = false;

			// The target's TAA sharpness, in [0, 1]; zero skips RCAS. Set only on a frame the resolve
			// ran below a render scale of 1, whose output is on the backbuffer's grid.
			float taaSharpness = 0.0f;

			// The target's, validated when it was set: the curve, and each effect's settings.
			PostProcess postProcess = FilmicPostProcess();

			// How many frames have begun on the target, which is what a grain pattern is held by.
			uint64_t frameCount = 0;
		};

		explicit PostProcessPass(const PassInitContext& ctx);
		~PostProcessPass() noexcept { spdlog::trace("~PostProcessPass"); }

		PostProcessPass(const PostProcessPass&) noexcept = delete;
		PostProcessPass(PostProcessPass&&) noexcept      = delete;

		PostProcessPass&
		operator=(const PostProcessPass&) noexcept = delete;

		PostProcessPass&
		operator=(PostProcessPass&&) noexcept = delete;

		/** @pre the constructor's batch has been built. Fatal on a binder name the PSO lacks. */
		void
		CheckBindings() const;

		void
		AttachToFrameGraph(FrameGraph& fg, const Args& args);

	private:
		void
		Execute(const Args& args, const PassContext& resources);

		bgpu::MeshletKernel m_Kernel;
	};
}
