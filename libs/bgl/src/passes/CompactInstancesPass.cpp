#include "passes/CompactInstancesPass.h"
#include "fg/FrameGraph.h"
#include "passes/DrawData.h"
#include "scene/AutoPoseState.h"
#include "scene/CullState.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "scene/scene_buffer_names.h"
#include <array>
#include <bgl/ISceneView.h>
#include <bgl/idl/Constants.h>
#include <bgl/idl/CullStats.h>
#include <bgl/idl/CullView.h>
#include <bgl/idl/DispatchArgs.h>
#include <bgl/idl/DrawBucket.h>
#include <bgpu/buffer/ComputeBuffer.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/pipeline/PipelineBatch.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/err/util.h>
#include <core/math.h>
#include <core/ref/SharedRef.h>
#include <span>
#include <spdlog/spdlog.h>

namespace bgl
{
	CompactInstancesPass::CompactInstancesPass(const PassInitContext& ctx) :
		m_CullStats(
			ctx.resourceManager,
			bgpu::ComputeBufferDesc().SetElement<idl::CullStats>().SetInitialCount(1).SetDebugName(
				"Cull Stats"))
	{
		core::ensure(ctx.device != nullptr, "Device pointer is null");

		ctx.pipelines->Add(
			m_ChoosePoses,
			bgpu::ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.culling.ChoosePoses"))
				.SetDebugName("Choose Poses"));

		ctx.pipelines->Add(
			m_GrantPoses,
			bgpu::ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.culling.GrantPoses"))
				.SetDebugName("Grant Poses"));

		ctx.pipelines->Add(
			m_CullInstances,
			bgpu::ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.culling.CullInstances"))
				.SetDebugName("Cull Instances"));

		ctx.pipelines->Add(
			m_Histogram,
			bgpu::ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.culling.HistogramInstances"))
				.SetDebugName("Histogram Instances"));

		ctx.pipelines->Add(
			m_PrefixSum,
			bgpu::ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.culling.PrefixSumInstances"))
				.SetDebugName("Prefix-Sum Instances"));

		ctx.pipelines->Add(
			m_CompactInstances,
			bgpu::ComputePipelineDesc()
				.SetShader(ctx.device->CreateShader("programs.culling.CompactInstances"))
				.SetDebugName("Compact Instances"));
	}

	void
	CompactInstancesPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		// Every other buffer named below is imported by the view: the cull inputs under its own
		// scope, the cull outputs under the scope of the frustum this records for.
		fg.ImportGlobalBuffer(c_CullStatsName, m_CullStats.GetBufferHandle());

		AttachClear(fg, draw);

		// The camera's cull alone chooses an automatic placement's pose source: a palette is per
		// view, so it is posed once whatever else culls it.
		const auto* view = draw.view->As<SceneView>();
		if (draw.cullIdx == 0 && view != nullptr && view->GetAutoPose().GetPlacementCount() > 0)
		{
			fg.AddPass(
				PassDesc()
					.SetName("Choose Poses {}", draw.drawIdx)
					.AddBufferRead(c_AutoPlacementsName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferRead(c_MeshInstanceBufferName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferRead(c_GeomBufferName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferRead(c_PlaybackArenaBufferName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferRead(c_RigBufferName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferRead(c_ClipBufferName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferRead(c_BlendNodeBufferName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferRead(c_BlendSampleBufferName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferRead(c_CullViewName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferRead(c_InstanceLodPreviousName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferReadWrite(c_InstanceLodName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferReadWrite(c_InstancePoseName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferReadWrite(c_PosePoolName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferReadWrite(c_AutoPosedName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferReadWrite(c_PoseRequestsName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferReadWrite(c_DominantFramesName, bgpu::BarrierSyncFlag::kComputeShader)
					.SetExec(
						[draw, this](const PassContext& ctx) { ExecuteChoosePoses(ctx, draw); }));
		}

		AttachCull(fg, draw);
	}

	void
	CompactInstancesPass::AttachClear(FrameGraph& fg, const DrawData& draw)
	{
		fg.AddPass(
			PassDesc()
				.SetName("Compact Instances Update {}.{}", draw.drawIdx, draw.cullIdx)
				.AddCopyDest(c_DrawBucketPrefixSumName)
				.AddCopyDest(c_CompactDispatchArgsName)
				.AddCopyDest(c_CullViewName)
				.AddCopyDest(c_CullStatsName)
				.AddCopyDest(c_InstanceLodName)
				.AddCopyDest(c_InstanceLodPreviousName)
				.SetExec([draw, this](const PassContext& ctx) { ExecuteClear(ctx, draw); }));
	}

	void
	CompactInstancesPass::AttachCull(FrameGraph& fg, const DrawData& draw)
	{
		fg.AddPass(
			  PassDesc()
				  .SetName("Cull Instances {}.{}", draw.drawIdx, draw.cullIdx)
				  .AddBufferRead(c_InstanceBufferName, bgpu::BarrierSyncFlag::kComputeShader)
				  .AddBufferRead(c_MeshInstanceBufferName, bgpu::BarrierSyncFlag::kComputeShader)
				  .AddBufferRead(c_GeomBufferName, bgpu::BarrierSyncFlag::kComputeShader)
				  .AddBufferRead(c_SubmeshBufferName, bgpu::BarrierSyncFlag::kComputeShader)
				  .AddBufferRead(c_CullViewName, bgpu::BarrierSyncFlag::kComputeShader)
				  .AddBufferRead(c_InstanceLodPreviousName, bgpu::BarrierSyncFlag::kComputeShader)
				  .AddBufferRead(c_PlaybackArenaBufferName, bgpu::BarrierSyncFlag::kComputeShader)
				  .AddBufferReadWrite(c_InstanceLodName, bgpu::BarrierSyncFlag::kComputeShader)
				  .AddBufferReadWrite(
					  c_InstanceVisibilityName,
					  bgpu::BarrierSyncFlag::kComputeShader)
				  .AddBufferReadWrite(c_CullStatsName, bgpu::BarrierSyncFlag::kComputeShader)
				  .SetExec([draw, this](const PassContext& ctx) { ExecuteCull(ctx, draw); }))
			.AddPass(
				PassDesc()
					.SetName("Histogram and Prefix Sum Instances {}.{}", draw.drawIdx, draw.cullIdx)
					.AddBufferRead(c_InstanceBufferName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferReadWrite(
						c_InstanceVisibilityName,
						bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferReadWrite(
						c_DrawBucketPrefixSumName,
						bgpu::BarrierSyncFlag::kComputeShader)
					.SetExec([draw, this](const PassContext& ctx) {
						ExecuteHistogramAndPrefixSum(ctx, draw);
					}))
			.AddPass(
				PassDesc()
					.SetName("Compact Instances {}.{}", draw.drawIdx, draw.cullIdx)
					.AddBufferRead(c_InstanceBufferName, bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferReadWrite(
						c_InstanceVisibilityName,
						bgpu::BarrierSyncFlag::kComputeShader)
					// Only the visible instances are written, at offsets the prefix sum decides, so
					// a stale entry left over from the previous frame is a plausible draw.
					.AddPoisonedBufferArg(
						c_CompactedInstancesName,
						bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferReadWrite(
						c_DrawBucketPrefixSumName,
						bgpu::BarrierSyncFlag::kComputeShader)
					.AddBufferReadWrite(
						c_CompactDispatchArgsName,
						bgpu::BarrierSyncFlag::kComputeShader)
					.SetExec([draw, this](const PassContext& ctx) {
						ExecuteGenerateInstanceDispatchArgs(ctx, draw);
					}));
	}

	void
	CompactInstancesPass::ExecuteClear(const PassContext& ctx, const DrawData& draw)
	{
		auto cmd = ctx.GetCommandList();

		core::ensure(draw.cullState != nullptr, "Compact pass requires the draw's cull state");

		draw.cullState->GetDrawBucketPrefixSum().Clear(cmd);
		m_CullStats.Clear(cmd);

		if (draw.cullState->TakeLodClear())
		{
			draw.cullState->GetInstanceLod().Clear(cmd);
			draw.cullState->GetPreviousInstanceLod().Clear(cmd);
		}
		draw.cullState->ClearFresh(cmd);

		// Assigned here rather than at attach time: a view drawn twice in one frame shares this
		// state, and each draw's cull must run against its own matrices.
		draw.cullState->GetCullView().Assign(std::span(&draw.viewState.cullView, 1));
		draw.cullState->GetCullView().Update(cmd);

		static constexpr std::array<idl::DispatchArgs, idl::cMaxDrawLanes> c_Seed = [] {
			std::array<idl::DispatchArgs, idl::cMaxDrawLanes> seed{};
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
	CompactInstancesPass::ExecuteChoosePoses(const PassContext& ctx, const DrawData& draw)
	{
		const auto*          view = draw.view->As<SceneView>();
		const AutoPoseState& pose = view->GetAutoPose();

		bgpu::Uniforms& choose      = m_ChoosePoses["gUniforms"];
		choose["autoPlacements"]    = ctx.GetBuffer(c_AutoPlacementsName);
		choose["meshBuffer"]        = ctx.GetBuffer(c_MeshInstanceBufferName);
		choose["geomBuffer"]        = ctx.GetBuffer(c_GeomBufferName);
		choose["playbackBuffer"]    = ctx.GetBuffer(c_PlaybackArenaBufferName);
		choose["rigs"]              = ctx.GetBuffer(c_RigBufferName);
		choose["cullView"]          = ctx.GetBuffer(c_CullViewName);
		choose["lodPrevious"]       = ctx.GetBuffer(c_InstanceLodPreviousName);
		choose["lodCurrent"]        = ctx.GetBuffer(c_InstanceLodName);
		choose["instancePose"]      = ctx.GetBuffer(c_InstancePoseName);
		choose["pool"]              = ctx.GetBuffer(c_PosePoolName);
		choose["posed"]             = ctx.GetBuffer(c_AutoPosedName);
		choose["requests"]          = ctx.GetBuffer(c_PoseRequestsName);
		choose["placementCount"]    = pose.GetPlacementCount();
		choose["poolStart"]         = pose.GetPoolStart();
		choose["budget"]            = pose.GetBudget();
		choose["time"]              = draw.clock.time;
		choose["prevTime"]          = draw.clock.prevTime;
		choose["dominantFrames"]    = ctx.GetBuffer(c_DominantFramesName);
		choose["clipBuffer"]        = ctx.GetBuffer(c_ClipBufferName);
		choose["blendNodeBuffer"]   = ctx.GetBuffer(c_BlendNodeBufferName);
		choose["blendSampleBuffer"] = ctx.GetBuffer(c_BlendSampleBufferName);

		auto cmdList = ctx.GetCommandList();

		auto computeState   = bgpu::ComputeState();
		computeState.kernel = &m_ChoosePoses;
		cmdList->SetComputeState(computeState);

		const uint32_t groups = core::div_ceil(pose.GetPlacementCount(), idl::cHistogramGroupSize);
		cmdList->Dispatch(groups, 1, 1);

		// The grants read the counters and the requests every placement's thread above wrote, and
		// rewrite the words they wrote; both run in this one pass, so the barrier between them is the
		// pass's own.
		for (const auto name : { c_PosePoolName, c_InstanceLodName, c_PoseRequestsName })
		{
			cmdList->Barrier(
				ctx.GetBuffer(name),
				bgpu::BufferBarrierDesc()
					.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
					.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
					.AddSyncAfter(bgpu::BarrierSyncFlag::kComputeShader)
					.AddAccessAfter(bgpu::BarrierAccessFlag::kUnorderedAccess));
		}

		bgpu::Uniforms& grant = m_GrantPoses["gUniforms"];
		grant["pool"]         = ctx.GetBuffer(c_PosePoolName);
		grant["requests"]     = ctx.GetBuffer(c_PoseRequestsName);
		grant["lodCurrent"]   = ctx.GetBuffer(c_InstanceLodName);
		grant["budget"]       = pose.GetBudget();

		computeState.kernel = &m_GrantPoses;
		cmdList->SetComputeState(computeState);
		cmdList->Dispatch(groups, 1, 1);
	}

	void
	CompactInstancesPass::ExecuteCull(const PassContext& ctx, const DrawData& draw)
	{
		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		bgpu::Uniforms& uniforms   = m_CullInstances["gUniforms"];
		uniforms["cullView"]       = ctx.GetBuffer(c_CullViewName);
		uniforms["instanceBuffer"] = ctx.GetBuffer(c_InstanceBufferName);
		uniforms["meshBuffer"]     = ctx.GetBuffer(c_MeshInstanceBufferName);
		uniforms["geomBuffer"]     = ctx.GetBuffer(c_GeomBufferName);
		uniforms["submeshBuffer"]  = ctx.GetBuffer(c_SubmeshBufferName);
		uniforms["visibility"]     = ctx.GetBuffer(c_InstanceVisibilityName);
		uniforms["lodPrevious"]    = ctx.GetBuffer(c_InstanceLodPreviousName);
		uniforms["lodCurrent"]     = ctx.GetBuffer(c_InstanceLodName);
		uniforms["playbackBuffer"] = ctx.GetBuffer(c_PlaybackArenaBufferName);

		// The stats writes are gated to BERNINI_GPU_DEBUG, so a release build drops the handle from
		// the kernel's reflection; bind it only when it survived.
		uniforms["stats"].SetIfValid(ctx.GetBuffer(c_CullStatsName));

		auto cmdList = ctx.GetCommandList();

		auto computeState   = bgpu::ComputeState();
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

		auto computeState   = bgpu::ComputeState();
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
			bgpu::BufferBarrierDesc()
				.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
				.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
				.AddSyncAfter(bgpu::BarrierSyncFlag::kComputeShader)
				.AddAccessAfter(bgpu::BarrierAccessFlag::kUnorderedAccess));

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

		auto computeState   = bgpu::ComputeState();
		computeState.kernel = &m_CompactInstances;

		cmdList->SetComputeState(computeState);

		const auto instanceCount = draw.view->GetInstanceCount();
		cmdList->Dispatch(core::div_ceil(instanceCount, idl::cCompactGroupSize), 1, 1);
	}
}
