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

		// [tested, frustum-culled], cleared each draw. Debug-only, aggregated nowhere and read by
		// nothing on the CPU, so it stays device-wide rather than multiplying per frustum.
		ComputeBuffer m_CullStats;
	};
}
