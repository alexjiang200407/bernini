#pragma once
#include "pipeline/MeshletKernel.h"
#include "resource/Rtv.h"
#include "resource/Sampler.h"
#include "resource/Srv.h"
#include "types/ViewportState.h"
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
			SrvHandle sceneColor;
			SrvHandle motionVectors;
			SrvHandle depth;

			// The unjittered camera this frame and last -- this frame's inverse projection, and
			// this frame's view space into last frame's clip -- so the resolve can tell a surface's
			// own motion from the camera's at each pixel's depth. One camera stands for the frame,
			// so a frame of several draws leaves the pair invalid.
			glm::mat4 clipToView{ 1.0f };
			glm::mat4 viewToPrevClip{ 1.0f };
			glm::vec2 jitter{ 0.0f };
			bool      cameraPairValid = false;

			// Last frame's accumulation, and the one this frame writes. Distinct textures: a
			// resource cannot be an SRV and an RTV in the same pass.
			SrvHandle prevHistory;
			RtvHandle history;

			// Graph resource names, so the pass can declare the ping-pong halves it actually touches
			// this frame rather than both.
			std::string prevHistoryName;
			std::string historyName;

			SamplerHandle pointSampler;
			SamplerHandle linearSampler;

			// The output grid: the history's, and what this pass rasterizes over.
			Viewport viewport;

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

		TaaResolvePass() = default;
		~TaaResolvePass() noexcept { logger::trace("~TaaResolvePass"); }

		TaaResolvePass(const TaaResolvePass&) noexcept = delete;
		TaaResolvePass(TaaResolvePass&&) noexcept      = delete;

		TaaResolvePass&
		operator=(const TaaResolvePass&) noexcept = delete;

		TaaResolvePass&
		operator=(TaaResolvePass&&) noexcept = delete;

		void
		Release()
		{
			m_Kernel.Reset();
		}

		void
		Init(IDevice* device, PipelineBatch& pipelines);

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
