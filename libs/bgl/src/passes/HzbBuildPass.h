#pragma once
#include "passes/PassInitContext.h"
#include <bgpu/pipeline/MeshletKernel.h>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <string_view>

namespace bgl
{
	class FrameGraph;
	class PassContext;
	struct DrawData;

	/**
	 * Builds a frustum's HzbChain from the depth as the frame has left it: one full-screen draw
	 * per level, the first reducing the depth buffer itself, each texel the farthest (under
	 * reversed-Z the minimum) of the 2x2 it covers, clamped at the source's edge. One graph pass
	 * per level, so the scheduler sees each level read the one above and the pass timer prices the
	 * ladder on its own.
	 *
	 * Attached twice in a frame that culls by occlusion: after phase 1 of Forward World, for phase
	 * 2's test, and after the grass, for next frame's phase 1.
	 */
	class HzbBuildPass
	{
	public:
		explicit HzbBuildPass(const PassInitContext& ctx);
		~HzbBuildPass() noexcept { spdlog::trace("~HzbBuildPass"); }

		HzbBuildPass(const HzbBuildPass&) noexcept = delete;
		HzbBuildPass(HzbBuildPass&&) noexcept      = delete;

		HzbBuildPass&
		operator=(const HzbBuildPass&) noexcept = delete;

		HzbBuildPass&
		operator=(HzbBuildPass&&) noexcept = delete;

		/** @pre the constructor's batch has been built. Fatal on a binder name the PSO lacks. */
		void
		CheckBindings() const;

		/**
		 * Attaches the ladder's passes for `draw`'s cull state, named "HZB <label> <draw>.<level>"
		 * so the two builds of one frame stay distinct. Attaches nothing while the chain has no
		 * levels. @pre the draw's cull state's chain is imported under the current namespace.
		 */
		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw, std::string_view label);

	private:
		void
		ExecuteLevel(const DrawData& draw, uint32_t level, const PassContext& resources);

		bgpu::MeshletKernel m_Kernel;
	};
}
