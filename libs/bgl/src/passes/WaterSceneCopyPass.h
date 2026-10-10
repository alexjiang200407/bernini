#pragma once
#include "passes/PassInitContext.h"
#include <bgpu/pipeline/MeshletKernel.h>
#include <spdlog/spdlog.h>

namespace bgl
{
	class FrameGraph;
	class PassContext;
	struct DrawData;

	/**
	 * Copies the scene colour the opaque phases drew into the target's SceneColorCopy, over the
	 * view's viewport, right before Forward Water: the water draws into scene colour, so the copy is
	 * what it refracts through. See docs/water.md.
	 */
	class WaterSceneCopyPass
	{
	public:
		explicit WaterSceneCopyPass(const PassInitContext& ctx);
		~WaterSceneCopyPass() noexcept { spdlog::trace("~WaterSceneCopyPass"); }

		WaterSceneCopyPass(const WaterSceneCopyPass&) noexcept = delete;
		WaterSceneCopyPass(WaterSceneCopyPass&&) noexcept      = delete;

		WaterSceneCopyPass&
		operator=(const WaterSceneCopyPass&) noexcept = delete;

		WaterSceneCopyPass&
		operator=(WaterSceneCopyPass&&) noexcept = delete;

		/** @pre the constructor's batch has been built. Fatal on a binder name the PSO lacks. */
		void
		CheckBindings() const;

		/** Attaches nothing when `draw` holds no copy to draw into. */
		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

		void
		Execute(const DrawData& draw, const PassContext& resources);

	private:
		bgpu::MeshletKernel m_Kernel;
	};
}
