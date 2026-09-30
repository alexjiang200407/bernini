#pragma once
#include "passes/PassInitContext.h"
#include <bgl/Viewport.h>
#include <bgl/glm.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/types/Rect.h>
#include <bgpu/types/Viewport.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <span>
#include <spdlog/spdlog.h>
#include <string>

namespace bgpu
{}

namespace bgpu
{
	class IDevice;
	class PipelineBatch;
}

namespace bgl
{
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
			bgpu::BufferHandle vertices;
			bgpu::BufferHandle indices;
			uint32_t           triangleCount = 0;
			bgpu::SrvHandle    texture;
			glm::vec2          translation{ 0.0f };
			glm::mat4          transform{ 1.0f };
			bgpu::Rect         scissor;
		};

		struct Args
		{
			// Every overlay a draw below belongs to; each is flushed before the first draw.
			std::span<const core::SharedRef<Overlay>> overlays;
			std::span<const Draw>                     draws;

			bgpu::RtvHandle     backBuffer;
			bgpu::Viewport      viewport;
			bgpu::SamplerHandle sampler;
		};

		OverlayPass() = default;
		~OverlayPass() noexcept { spdlog::trace("~OverlayPass"); }

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
		Init(const PassInitContext& ctx);

		/** @pre the batch Init requested into has been built. Fatal on a binder name the PSO lacks. */
		void
		CheckBindings() const;

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

		bgpu::MeshletKernel m_Kernel;
	};
}
