#pragma once
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "scene/ComputeBuffer.h"
#include "uniforms/Uniforms.h"

namespace bgl
{
	class FrameGraph;
	class IDevice;
	class IResourceManager;
	class PassContext;
	struct DrawData;

	class CompactInstancesPass
	{
	public:
		CompactInstancesPass() = default;
		~CompactInstancesPass() noexcept { logger::trace("~CompactInstancesPass"); }
		CompactInstancesPass(IDevice* device, core::SharedRef<IResourceManager> resourceManager)
		{
			Init(device, resourceManager);
		}

		CompactInstancesPass(const CompactInstancesPass&) noexcept = delete;
		CompactInstancesPass(CompactInstancesPass&&) noexcept      = delete;

		CompactInstancesPass&
		operator=(const CompactInstancesPass&) noexcept = delete;

		CompactInstancesPass&
		operator=(CompactInstancesPass&&) noexcept = delete;

		void
		Init(IDevice* device, core::SharedRef<IResourceManager> resourceManager);

		void
		Release(bool deferred = true);

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

	private:
		void
		ExecuteClear(const PassContext& ctx, const DrawData& draw);

		void
		ExecuteCull(const PassContext& ctx, const DrawData& draw);

		void
		ExecuteHistogramAndPrefixSum(const PassContext& ctx, const DrawData& draw);

		void
		ExecuteGenerateInstanceDispatchArgs(const PassContext& ctx, const DrawData& draw);

	private:
		ComputeKernel m_CullInstances;
		ComputeKernel m_Histogram;
		ComputeKernel m_PrefixSum;
		ComputeKernel m_CompactInstances;

		// These size of these buffers is PsoType::kCount
		// Not tied to the scene
		ComputeBuffer m_CompactedDispatchArgs;
		ComputeBuffer m_PsoPrefixSumBuffer;

		// One CullView per draw, uploaded before the cull dispatch reads it. Per-context, not
		// per-view: rewritten each draw.
		ComputeBuffer m_CullView;

		// [tested, frustum-culled], cleared each draw and read back for the stats overlay.
		ComputeBuffer m_CullStats;
	};
}
