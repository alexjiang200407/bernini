#pragma once
#include "passes/PassInitContext.h"
#include <bgpu/pipeline/ComputeKernel.h>
#include <spdlog/spdlog.h>

namespace bgl
{
	class FrameGraph;
	class PassContext;
	struct DrawData;

	/**
	 * Selects the placements holding a toon shading rig that this draw can see well enough to shade
	 * -- visible, their head larger on screen than the rig's fade end, at most the pool's capacity --
	 * and evaluates each rig against the sun in that placement's head space into the view's pool,
	 * marking the placement's flags word with the block it took. One thread per rigged placement,
	 * after the pose pass, whose palettes give a head bone its pose, and before the forward passes,
	 * which read the blocks. See docs/toon_shading_rig.md.
	 */
	class ToonShadingRigPass
	{
	public:
		explicit ToonShadingRigPass(const PassInitContext& ctx);
		~ToonShadingRigPass() noexcept { spdlog::trace("~ToonShadingRigPass"); }

		ToonShadingRigPass(const ToonShadingRigPass&) noexcept = delete;
		ToonShadingRigPass(ToonShadingRigPass&&) noexcept      = delete;

		ToonShadingRigPass&
		operator=(const ToonShadingRigPass&) noexcept = delete;

		ToonShadingRigPass&
		operator=(ToonShadingRigPass&&) noexcept = delete;

		/** Adds nothing to a view whose placements hold no rig. */
		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

	private:
		void
		Execute(const PassContext& ctx, const DrawData& draw);

		bgpu::ComputeKernel m_Evaluate;
	};
}
