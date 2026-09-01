#pragma once
#include "pipeline/MeshletKernel.h"
#include "resource/Buffer.h"
#include "resource/Rtv.h"
#include "resource/Sampler.h"
#include "resource/Srv.h"
#include "types/Rect.h"
#include "types/Viewport.h"
#include <bgl/glm.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class IDevice;
	class FrameGraph;
	class PassContext;
	class Overlay;

	/**
	 * Draws a frame's 2D overlay onto the backbuffer after PostProcess: one mesh dispatch per
	 * draw, reading its triangles from the geometry's bindless buffers, blended premultiplied over
	 * what the tonemap wrote. Attached only on a frame that submitted draws.
	 */
	class OverlayPass
	{
	public:
		// A draw with every handle resolved at submission, so what the pass records is what the
		// caller named at DrawOverlay.
		struct Draw
		{
			BufferHandle vertices;
			BufferHandle indices;
			uint32_t     triangleCount = 0;
			SrvHandle    texture;
			glm::vec2    translation{ 0.0f };
			glm::mat4    transform{ 1.0f };
			Rect         scissor;
		};

		struct Args
		{
			// Every overlay a draw below belongs to; each is flushed before the first draw.
			std::span<const core::SharedRef<Overlay>> overlays;
			std::span<const Draw>                     draws;

			RtvHandle     backBuffer;
			Viewport      viewport;
			SamplerHandle sampler;
		};

		OverlayPass() = default;
		~OverlayPass() noexcept { logger::trace("~OverlayPass"); }

		OverlayPass(const OverlayPass&) noexcept = delete;
		OverlayPass(OverlayPass&&) noexcept      = delete;

		OverlayPass&
		operator=(const OverlayPass&) noexcept = delete;

		OverlayPass&
		operator=(OverlayPass&&) noexcept = delete;

		void
		Release()
		{
			m_Kernel.Reset();
		}

		void
		Init(IDevice* device);

		/**
		 * `sources` are the imported names of every other target a draw samples, declared as reads
		 * so the graph barriers them into shader-resource before the first dispatch. Consumed here;
		 * the exec keeps only `args`.
		 */
		void
		AttachToFrameGraph(FrameGraph& fg, const Args& args, std::span<const std::string> sources);

	private:
		void
		Execute(const Args& args, const PassContext& resources);

		MeshletKernel m_Kernel;
	};
}
