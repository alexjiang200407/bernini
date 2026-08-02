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
	 * Applies the display curve to the linear HDR scene colour and writes the backbuffer.
	 *
	 * It is the frame's last colour pass and the only writer of the backbuffer, which is what keeps
	 * the capture path -- a readback of the last presented backbuffer -- describing what was shown.
	 * Exposure is not applied here: it is a per-view scale and the geometry passes have already
	 * folded it in, while a target may carry several views.
	 */
	class TonemapPass
	{
	public:
		struct Args
		{
			SrvHandle     sceneColor;
			RtvHandle     backBuffer;
			SamplerHandle sampler;
			Viewport      viewport;
		};

		TonemapPass() = default;
		~TonemapPass() noexcept { logger::trace("~TonemapPass"); }

		TonemapPass(const TonemapPass&) noexcept = delete;
		TonemapPass(TonemapPass&&) noexcept      = delete;

		TonemapPass&
		operator=(const TonemapPass&) noexcept = delete;

		TonemapPass&
		operator=(TonemapPass&&) noexcept = delete;

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
