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
	 * Accumulates this frame's jittered scene colour into the temporal history, reprojecting the
	 * previous one through the velocity buffer and clamping it to the 3x3 neighbourhood.
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
			Viewport      viewport;

			// False on the first frame and the first after a resize; the resolve then takes the
			// scene colour whole rather than blending against an accumulation that does not exist.
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
		Init(IDevice* device);

		void
		AttachToFrameGraph(FrameGraph& fg, const Args& args);

	private:
		void
		Execute(const Args& args, const PassContext& resources);

		MeshletKernel m_Kernel;
	};
}
