#pragma once
#include "passes/PassInitContext.h"
#include <bgpu/buffer/ComputeBuffer.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/ref/SharedRef.h>
#include <spdlog/spdlog.h>
#include <string_view>

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
		explicit CompactInstancesPass(const PassInitContext& ctx);
		~CompactInstancesPass() noexcept { spdlog::trace("~CompactInstancesPass"); }

		CompactInstancesPass(const CompactInstancesPass&) noexcept = delete;
		CompactInstancesPass(CompactInstancesPass&&) noexcept      = delete;

		CompactInstancesPass&
		operator=(const CompactInstancesPass&) noexcept = delete;

		CompactInstancesPass&
		operator=(CompactInstancesPass&&) noexcept = delete;

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

		/**
		 * Phase 2 of the occlusion cull: tests phase 1's candidates against the HZB built from
		 * phase 1's depth, writing phase 2's visibility words, then compacts them. @pre the graph's
		 * namespace is the frustum's cull scope, where phase 1 recorded; it is the phase-2 scope on
		 * return, where Forward World Phase 2 records, and the caller sets it back.
		 */
		void
		AttachPhase2(FrameGraph& fg, const DrawData& draw, std::string_view cullScope);

	private:
		void
		ExecuteClear(const PassContext& ctx, const DrawData& draw);

		void
		ExecuteCull(const PassContext& ctx, const DrawData& draw);

		void
		ExecuteChoosePoses(const PassContext& ctx, const DrawData& draw);

		void
		AttachClear(FrameGraph& fg, const DrawData& draw);

		void
		AttachCull(FrameGraph& fg, const DrawData& draw);

		/** The counting sort and the compaction over the visibility words the namespace names. */
		void
		AttachCompaction(FrameGraph& fg, const DrawData& draw, std::string_view phase);

		void
		ExecuteCullOccluded(const PassContext& ctx, const DrawData& draw);

		void
		ExecuteHistogramAndPrefixSum(const PassContext& ctx, const DrawData& draw);

		void
		ExecuteGenerateInstanceDispatchArgs(const PassContext& ctx, const DrawData& draw);

	private:
		bgpu::ComputeKernel m_ChoosePoses;
		bgpu::ComputeKernel m_GrantPoses;
		bgpu::ComputeKernel m_CullInstances;
		bgpu::ComputeKernel m_CullOccluded;
		bgpu::ComputeKernel m_Histogram;
		bgpu::ComputeKernel m_PrefixSum;
		bgpu::ComputeKernel m_CompactInstances;

		// [tested, frustum-culled], cleared each draw. Debug-only, aggregated nowhere and read by
		// nothing on the CPU, so it stays device-wide rather than multiplying per frustum.
		bgpu::ComputeBuffer m_CullStats;
	};
}
