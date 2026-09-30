#pragma once
#include "passes/PassInitContext.h"
#include <bgpu/buffer/ComputeBuffer.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/ref/SharedRef.h>
#include <spdlog/spdlog.h>

namespace bgpu
{
	class PipelineBatch;
	class IDevice;
	class IResourceManager;
}

namespace bgl
{
	class FrameGraph;
	class PassContext;
	struct DrawData;

	class CompactInstancesPass
	{
	public:
		CompactInstancesPass() = default;
		~CompactInstancesPass() noexcept { spdlog::trace("~CompactInstancesPass"); }

		CompactInstancesPass(const CompactInstancesPass&) noexcept = delete;
		CompactInstancesPass(CompactInstancesPass&&) noexcept      = delete;

		CompactInstancesPass&
		operator=(const CompactInstancesPass&) noexcept = delete;

		CompactInstancesPass&
		operator=(CompactInstancesPass&&) noexcept = delete;

		void
		Init(const PassInitContext& ctx);

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
		bgpu::ComputeKernel m_CullInstances;
		bgpu::ComputeKernel m_Histogram;
		bgpu::ComputeKernel m_PrefixSum;
		bgpu::ComputeKernel m_CompactInstances;

		// [tested, frustum-culled], cleared each draw. Debug-only, aggregated nowhere and read by
		// nothing on the CPU, so it stays device-wide rather than multiplying per frustum.
		bgpu::ComputeBuffer m_CullStats;
	};
}
