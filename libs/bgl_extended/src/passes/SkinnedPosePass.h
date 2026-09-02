#pragma once
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"

namespace bgl
{
	class PipelineBatch;

	class FrameGraph;
	class IDevice;
	class PassContext;
	struct DrawData;

	/**
	 * Writes every skinned instance's bone palette for this draw: one workgroup per instance, sampling
	 * its clip and walking its rig's hierarchy. Ordered ahead of the forward pass, which reads what it
	 * wrote.
	 *
	 * Each instance gets two palettes, at `time` and at `prevTime`, so the skinned mesh shader can
	 * write a motion vector without a history buffer. That holds only while time is the sole input to a
	 * pose -- a clip switched between frames would reproject through the wrong clip.
	 */
	class SkinnedPosePass
	{
	public:
		SkinnedPosePass() = default;
		~SkinnedPosePass() noexcept { logger::trace("~SkinnedPosePass"); }

		SkinnedPosePass(const SkinnedPosePass&) noexcept = delete;
		SkinnedPosePass(SkinnedPosePass&&) noexcept      = delete;

		SkinnedPosePass&
		operator=(const SkinnedPosePass&) noexcept = delete;

		SkinnedPosePass&
		operator=(SkinnedPosePass&&) noexcept = delete;

		void
		Init(IDevice* device, PipelineBatch& pipelines);

		void
		Release();

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

	private:
		void
		Execute(const PassContext& ctx, const DrawData& draw);

		ComputeKernel m_PoseSkinned;
	};
}
