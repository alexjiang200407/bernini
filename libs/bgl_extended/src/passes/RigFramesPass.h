#pragma once
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include <spdlog/spdlog.h>

namespace bgl
{
	class PipelineBatch;

	class FrameGraph;
	class IDevice;
	class PassContext;
	struct DrawData;

	/**
	 * Fills the bone anim table of every rig that has been given one and not yet posed into it: one
	 * dispatch per rig, one workgroup per frame of its clip set. Ordered ahead of the forward pass,
	 * which reads what it wrote.
	 *
	 * Attached on almost no frame, and absent from the graph entirely on the rest: a scene that
	 * draws no crowd instance pays nothing for this pass, not even the barriers its arguments would
	 * declare. A rig is filled when the first instance drawing from its table is spawned, and again
	 * only if the arena grows, which discards what it held.
	 */
	class RigFramesPass
	{
	public:
		RigFramesPass() = default;
		~RigFramesPass() noexcept { logger::trace("~RigFramesPass"); }

		RigFramesPass(const RigFramesPass&) noexcept = delete;
		RigFramesPass(RigFramesPass&&) noexcept      = delete;

		RigFramesPass&
		operator=(const RigFramesPass&) noexcept = delete;

		RigFramesPass&
		operator=(RigFramesPass&&) noexcept = delete;

		void
		Init(IDevice* device, PipelineBatch& pipelines);

		void
		Release();

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

	private:
		void
		Execute(const PassContext& ctx, const DrawData& draw);

		ComputeKernel m_PoseRigFrames;
	};
}
