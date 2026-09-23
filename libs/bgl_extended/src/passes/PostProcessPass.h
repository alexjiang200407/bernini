#pragma once
#include "passes/PassInitContext.h"
#include "pipeline/MeshletKernel.h"
#include "resource/Rtv.h"
#include "resource/Sampler.h"
#include "resource/Srv.h"
#include "types/ViewportState.h"
#include <bgl/IRenderTarget.h>
#include <bgl/Viewport.h>
#include <spdlog/spdlog.h>
#include <string>

namespace bgl
{
	class PipelineBatch;

	class IDevice;
	class FrameGraph;
	class PassContext;

	/**
	 * Turns the linear HDR scene colour into the displayed image: the frame's last colour pass over
	 * the scene, and the first writer of the backbuffer, which it covers whole. Only the overlay
	 * writes it afterwards, blending over this -- so the capture path, a readback of the last
	 * presented backbuffer, still describes what was shown.
	 *
	 * Today that is the bloom combine, the colour grade and the display curve. Everything between a
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
			SrvHandle   source;
			std::string sourceName;
			RtvHandle   backBuffer;

			// Point where the source is already on the backbuffer's grid, which is every frame the
			// resolve ran and every unscaled one; linear is what carries a render-resolution scene
			// colour across when it did not.
			SamplerHandle sampler;
			Viewport      viewport;

			// Set only when an outline-mask pass ran this frame; the shader samples the mask
			// behind the flag, so a disabled frame binds nothing. The mask is on the render grid
			// and its dilate is a coverage test, so it is point-sampled whatever the source is.
			SrvHandle     outlineMask;
			SamplerHandle maskSampler;
			glm::vec2     maskSize{ 0.0f };
			bool          outlineEnabled = false;

			// The display curve's LUT, and the linear clamp it is read through.
			SrvHandle     tonemapLut;
			SamplerHandle lutSampler;

			// Set only when the bloom passes ran this frame; the shader samples the chain behind
			// the flag, so a disabled frame binds nothing. Half the source's resolution, so it is
			// always linearly sampled.
			SrvHandle     bloom;
			SamplerHandle bloomSampler;
			std::string   bloomName;
			float         bloomIntensity = 0.0f;
			bool          bloomEnabled   = false;

			// The target's TAA sharpness, in [0, 1]; zero skips RCAS. Set only on a frame the resolve
			// ran, whose output is on the backbuffer's grid.
			float taaSharpness = 0.0f;

			// Validated by the target that carries it; written only when enabled.
			ColorGradeSettings colorGrade;
			bool               colorGradeEnabled = false;
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
		Init(const PassInitContext& ctx);

		/** @pre the batch Init requested into has been built. Fatal on a binder name the PSO lacks. */
		void
		CheckBindings() const;

		void
		AttachToFrameGraph(FrameGraph& fg, const Args& args);

	private:
		void
		Execute(const Args& args, const PassContext& resources);

		MeshletKernel m_Kernel;
	};
}
