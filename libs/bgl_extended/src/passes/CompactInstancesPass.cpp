#include "passes/CompactInstancesPass.h"
#include "fg/FrameGraph.h"
#include "passes/DrawData.h"
#include "pipeline/ComputePipeline.h"
#include "pipeline/PipelineBatch.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/CullState.h"
#include "scene/Scene.h"
#include "scene/scene_buffer_names.h"
#include "types/Barrier.h"
#include "uniforms/Uniforms.h"
#include <array>
#include <bgl/ISceneView.h>
#include <bgl_common/gassert.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/CullStats.h>
#include <bgl_common/idl/CullView.h>
#include <bgl_common/idl/DispatchArgs.h>
#include <bgl_common/idl/DrawBucket.h>
#include <core/math.h>
#include <core/ref/SharedRef.h>
#include <span>
#include <spdlog/spdlog.h>

namespace bgl
{
	void
	CompactInstancesPass::Init(const PassInitContext& ctx)
	{
		gassert(ctx.device != nullptr, "Device pointer is null");

		ctx.pipelines->Add(
			m_CullInstances,
			ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.culling.CullInstances"))
				.SetDebugName("Cull Instances"));

		ctx.pipelines->Add(
			m_Histogram,
			ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.culling.HistogramInstances"))
				.SetDebugName("Histogram Instances"));

		ctx.pipelines->Add(
			m_PrefixSum,
			ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.culling.PrefixSumInstances"))
				.SetDebugName("Prefix-Sum Instances"));

		ctx.pipelines->Add(
			m_CompactInstances,
			ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.culling.CompactInstances"))
				.SetDebugName("Compact Instances"));

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<idl::CullStats>().SetInitialCount(1).SetDebugName("Cull Stats");

			m_CullStats.Init(desc, ctx.resourceManager);
		}
	}

	void
	CompactInstancesPass::Release(bool deferred)
	{
		logger::trace("CompactInstancesPass::Release");

		m_CullInstances.Reset();
		m_Histogram.Reset();
		m_PrefixSum.Reset();
		m_CompactInstances.Reset();

		m_CullStats.Release(deferred);
	}

	void
	CompactInstancesPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		// Every other buffer named below is imported by the view: the cull inputs under its own
		// scope, the cull outputs under the scope of the frustum this records for.
		fg.ImportGlobalBuffer(c_CullStatsName, m_CullStats.GetBufferHandle())
			.AddPass(
				PassDesc()
					.SetName("Compact Instances Update {}.{}", draw.drawIdx, draw.cullIdx)
					.AddBufferArg(
						c_DrawBucketPrefixSumName,
						BarrierSyncFlag::kCopy,
						BarrierAccessFlag::kCopyDest)
					.AddBufferArg(
						c_CompactDispatchArgsName,
						BarrierSyncFlag::kCopy,
						BarrierAccessFlag::kCopyDest)
					.AddBufferArg(
						c_CullViewName,
						BarrierSyncFlag::kCopy,
						BarrierAccessFlag::kCopyDest)
					.AddBufferArg(
						c_CullStatsName,
						BarrierSyncFlag::kCopy,
						BarrierAccessFlag::kCopyDest)
					.SetExec([draw, this](const PassContext& ctx) { ExecuteClear(ctx, draw); }))
			.AddPass(
				PassDesc()
					.SetName("Cull Instances {}.{}", draw.drawIdx, draw.cullIdx)
					.AddBufferArg(
						c_InstanceBufferName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						c_MeshInstanceBufferName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						c_GeomBufferName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						c_SubmeshBufferName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						c_CullViewName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						c_InstanceVisibilityName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						c_CullStatsName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) { ExecuteCull(ctx, draw); }))
			.AddPass(
				PassDesc()
					.SetName("Histogram and Prefix Sum Instances {}.{}", draw.drawIdx, draw.cullIdx)
					.AddBufferArg(
						c_InstanceBufferName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						c_InstanceVisibilityName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						c_DrawBucketPrefixSumName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) {
						ExecuteHistogramAndPrefixSum(ctx, draw);
					}))
			.AddPass(
				PassDesc()
					.SetName("Compact Instances {}.{}", draw.drawIdx, draw.cullIdx)
					.AddBufferArg(
						c_InstanceBufferName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						c_InstanceVisibilityName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					// Only the visible instances are written, at offsets the prefix sum decides, so
					// a stale entry left over from the previous frame is a plausible draw.
					.AddPoisonedBufferArg(c_CompactedInstancesName, BarrierSyncFlag::kComputeShader)
					.AddBufferArg(
						c_DrawBucketPrefixSumName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						c_CompactDispatchArgsName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) {
						ExecuteGenerateInstanceDispatchArgs(ctx, draw);
					}));
	}

	void
	CompactInstancesPass::ExecuteClear(const PassContext& ctx, const DrawData& draw)
	{
		auto cmd = ctx.GetCommandList();

		gassert(draw.cullState != nullptr, "Compact pass requires the draw's cull state");

		draw.cullState->GetDrawBucketPrefixSum().Clear(cmd);
		m_CullStats.Clear(cmd);

		// Assigned here rather than at attach time: a view drawn twice in one frame shares this
		// state, and each draw's cull must run against its own matrices.
		draw.cullState->GetCullView().Assign(std::span(&draw.viewState.cullView, 1));
		draw.cullState->GetCullView().Update(cmd);

		static constexpr std::array<idl::DispatchArgs, idl::cMaxDrawBuckets> c_Seed = [] {
			std::array<idl::DispatchArgs, idl::cMaxDrawBuckets> seed{};
			for (idl::DispatchArgs& args : seed)
			{
				args = { 0u, 1u, 1u };
			}
			return seed;
		}();

		cmd->WriteBuffer(
			draw.cullState->GetCompactedDispatchArgs().GetBufferHandle(),
			c_Seed.data(),
			sizeof(c_Seed));
	}

	void
	CompactInstancesPass::ExecuteCull(const PassContext& ctx, const DrawData& draw)
	{
		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		Uniforms& uniforms         = m_CullInstances["gUniforms"];
		uniforms["cullView"]       = ctx.GetBuffer(c_CullViewName);
		uniforms["instanceBuffer"] = ctx.GetBuffer(c_InstanceBufferName);
		uniforms["meshBuffer"]     = ctx.GetBuffer(c_MeshInstanceBufferName);
		uniforms["geomBuffer"]     = ctx.GetBuffer(c_GeomBufferName);
		uniforms["submeshBuffer"]  = ctx.GetBuffer(c_SubmeshBufferName);
		uniforms["visibility"]     = ctx.GetBuffer(c_InstanceVisibilityName);

		// The stats writes are gated to BERNINI_GPU_DEBUG, so a release build drops the handle from
		// the kernel's reflection; bind it only when it survived.
		uniforms["stats"].SetIfValid(ctx.GetBuffer(c_CullStatsName));

		auto cmdList = ctx.GetCommandList();

		auto computeState   = ComputeState();
		computeState.kernel = &m_CullInstances;

		cmdList->SetComputeState(computeState);

		const auto instanceCount = draw.view->GetInstanceCount();
		cmdList->Dispatch(core::div_ceil(instanceCount, idl::cHistogramGroupSize), 1, 1);
	}

	void
	CompactInstancesPass::ExecuteHistogramAndPrefixSum(const PassContext& ctx, const DrawData& draw)
	{
		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		auto instanceBuffer            = ctx.GetBuffer(c_InstanceBufferName);
		auto drawBucketPrefixSumBuffer = ctx.GetBuffer(c_DrawBucketPrefixSumName);

		m_Histogram["gUniforms"]["instanceBuffer"] = instanceBuffer;
		m_Histogram["gUniforms"]["visibility"]     = ctx.GetBuffer(c_InstanceVisibilityName);

		// Reuse histogram buffer as prefix sum buffer
		m_Histogram["gUniforms"]["outBuffer"] = drawBucketPrefixSumBuffer;

		auto cmdList = ctx.GetCommandList();

		auto computeState   = ComputeState();
		computeState.kernel = &m_Histogram;

		cmdList->SetComputeState(computeState);

		const auto instanceCount = draw.view->GetInstanceCount();
		cmdList->Dispatch(core::div_ceil(instanceCount, idl::cHistogramGroupSize), 1, 1);

		// The histogram writes drawBucketPrefixSum (UAV); the prefix-sum scan below reads and
		// rewrites the same buffer. Both dispatches run back-to-back inside this single
		// frame-graph pass, so no pass-boundary barrier separates them -- insert an
		// explicit UAV barrier or the scan races the histogram. The race only corrupts
		// results with multiple buckets (a lone bucket's base is the prefix sum of
		// prior, empty buckets, which is always 0), which is why it shows up as
		// flickering only in scenes mixing buckets.
		cmdList->Barrier(
			drawBucketPrefixSumBuffer,
			BufferBarrierDesc()
				.AddSyncBefore(BarrierSyncFlag::kComputeShader)
				.AddAccessBefore(BarrierAccessFlag::kUnorderedAccess)
				.AddSyncAfter(BarrierSyncFlag::kComputeShader)
				.AddAccessAfter(BarrierAccessFlag::kUnorderedAccess));

		m_PrefixSum["gUniforms"]["inOutBuffer"] = drawBucketPrefixSumBuffer;

		computeState.kernel = &m_PrefixSum;

		cmdList->SetComputeState(computeState);

		cmdList->Dispatch(1, 1, 1);
	}

	void
	CompactInstancesPass::ExecuteGenerateInstanceDispatchArgs(
		const PassContext& ctx,
		const DrawData&    draw)
	{
		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		auto instanceBuffer              = ctx.GetBuffer(c_InstanceBufferName);
		auto compactedInstancesBuffer    = ctx.GetBuffer(c_CompactedInstancesName);
		auto drawBucketPrefixSumBuffer   = ctx.GetBuffer(c_DrawBucketPrefixSumName);
		auto compactedDispatchArgsBuffer = ctx.GetBuffer(c_CompactDispatchArgsName);

		m_CompactInstances["gUniforms"]["instanceBuffer"] = instanceBuffer;
		m_CompactInstances["gUniforms"]["visibility"]     = ctx.GetBuffer(c_InstanceVisibilityName);
		m_CompactInstances["gUniforms"]["drawBucketPrefixSum"] = drawBucketPrefixSumBuffer;
		m_CompactInstances["gUniforms"]["compactedInstances"]  = compactedInstancesBuffer;
		m_CompactInstances["gUniforms"]["dispatchArgs"]        = compactedDispatchArgsBuffer;

		auto cmdList = ctx.GetCommandList();

		auto computeState   = ComputeState();
		computeState.kernel = &m_CompactInstances;

		cmdList->SetComputeState(computeState);

		const auto instanceCount = draw.view->GetInstanceCount();
		cmdList->Dispatch(core::div_ceil(instanceCount, idl::cCompactGroupSize), 1, 1);
	}
}
