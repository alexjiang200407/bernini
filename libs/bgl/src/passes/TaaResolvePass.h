#pragma once
#include "passes/PassInitContext.h"
#include <bgl/types/Viewport.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/types/ViewportState.h>
#include <core/glm.h>
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
	 * Accumulates this frame's jittered scene colour into the temporal history, reprojecting the
	 * previous one through the velocity buffer and clamping it to the 3x3 neighbourhood.
	 *
	 * The one pass that spans both of a target's grids: it reads the render-resolution frame and
	 * writes the output-resolution history, so a render scale is reconstructed here rather than
	 * stretched at present.
	 *
	 * It writes the history and nothing else. `PostProcess` reads what it produced and applies the
	 * display curve, so anything that must sit between a resolved scene and the screen -- bloom,
	 * grading -- has a stage to live in rather than arriving as a change to this shader.
	 */
	class TaaResolvePass
	{
	public:
		struct Args
		{
			bgpu::SrvHandle sceneColor;
			bgpu::SrvHandle motionVectors;
			bgpu::SrvHandle depth;

			// The unjittered camera this frame and last -- this frame's inverse projection, and
			// this frame's view space into last frame's -- so the resolve can tell whether a pixel's
			// surface was hidden last frame. One camera stands for the frame, so a frame of several
			// draws leaves the pair invalid.
			glm::mat4 clipToView{ 1.0f };
			glm::mat4 viewToPrevView{ 1.0f };
			glm::vec2 jitter{ 0.0f };
			bool      cameraPairValid = false;

			// Last frame's accumulation, and the one this frame writes. Distinct textures: a
			// resource cannot be an SRV and an RTV in the same pass.
			bgpu::SrvHandle prevHistory;
			bgpu::RtvHandle history;

			// Graph resource names, so the pass can declare the ping-pong halves it actually touches
			// this frame rather than both.
			std::string prevHistoryName;
			std::string historyName;

			bgpu::SamplerHandle pointSampler;
			bgpu::SamplerHandle linearSampler;

			// The output grid: the history's, and what this pass rasterizes over.
			bgpu::Viewport viewport;

			// The grid the scene colour, the velocity buffer and the depth are on. Equal to the
			// viewport's extent at render scale 1.0, where the resolve is a plain accumulation.
			glm::vec2 renderSize{ 0.0f };

			// The width, in output pixels, of the kernel an output pixel weights its nearest render
			// sample by. A no-op wherever the two grids coincide.
			float reconstructionWidth = 0.4f;

			// False on the first frame, the first after a resize, and the first after the scene's
			// shading changed; the resolve then takes the scene colour whole rather than blending
			// against an accumulation that describes something else.
			bool historyValid = false;
		};

		explicit TaaResolvePass(const PassInitContext& ctx);
		~TaaResolvePass() noexcept { spdlog::trace("~TaaResolvePass"); }

		TaaResolvePass(const TaaResolvePass&) noexcept = delete;
		TaaResolvePass(TaaResolvePass&&) noexcept      = delete;

		TaaResolvePass&
		operator=(const TaaResolvePass&) noexcept = delete;

		TaaResolvePass&
		operator=(TaaResolvePass&&) noexcept = delete;

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
