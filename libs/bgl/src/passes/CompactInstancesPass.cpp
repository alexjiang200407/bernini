#include "passes/CompactInstancesPass.h"
#include "fg/FrameGraph.h"
#include "idl/Constants.h"
#include "idl/CullView.h"
#include "idl/DispatchArgs.h"
#include "passes/DrawData.h"
#include "pipeline/ComputePipeline.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/CullState.h"
#include "scene/Scene.h"
#include <bgl/ISceneView.h>
#include <bgl/PsoType.h>
#include <core/math.h>

namespace bgl
{
	void
	CompactInstancesPass::Init(IDevice* device, core::SharedRef<IResourceManager> resourceManager)
	{
		gassert(device != nullptr, "Device pointer is null");

		m_CullInstances = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("CullInstances"))
				.SetDebugName("Cull Instances"));

		m_Histogram = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("HistogramInstances"))
				.SetDebugName("Histogram Instances"));

		m_PrefixSum = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("PrefixSumInstances"))
				.SetDebugName("Prefix-Sum Instances"));

		m_CompactInstances = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("CompactInstances"))
				.SetDebugName("Compact Instances"));

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<idl::CullStats>().SetInitialCount(1).SetDebugName("Cull Stats");

			m_CullStats.Init(desc, resourceManager);
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
		fg.ImportGlobalBuffer("cull.stats", m_CullStats.GetBufferHandle())
			.AddPass(
				PassDesc()
					.SetName(
						std::format("Compact Instances Update {}.{}", draw.drawIdx, draw.cullIdx))
					.AddBufferArg(
						"compactedInstances.psoPrefixSumBuffer",
						BarrierSyncFlag::kCopy,
						BarrierAccessFlag::kCopyDest)
					.AddBufferArg(
						"compactedInstances.compactDispatchArgs",
						BarrierSyncFlag::kCopy,
						BarrierAccessFlag::kCopyDest)
					.AddBufferArg("cull.view", BarrierSyncFlag::kCopy, BarrierAccessFlag::kCopyDest)
					.AddBufferArg(
						"cull.stats",
						BarrierSyncFlag::kCopy,
						BarrierAccessFlag::kCopyDest)
					.SetExec([draw, this](const PassContext& ctx) { ExecuteClear(ctx, draw); }))
			.AddPass(
				PassDesc()
					.SetName(std::format("Cull Instances {}.{}", draw.drawIdx, draw.cullIdx))
					.AddBufferArg(
						"scene.instanceBuffer",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						"scene.meshInstanceBuffer",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						"scene.submeshBuffer",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						"cull.view",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						"scene.instanceVisibility",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						"cull.stats",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) { ExecuteCull(ctx, draw); }))
			.AddPass(
				PassDesc()
					.SetName(
						std::format(
							"Histogram and Prefix Sum Instances {}.{}",
							draw.drawIdx,
							draw.cullIdx))
					.AddBufferArg(
						"scene.instanceBuffer",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						"scene.instanceVisibility",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						"compactedInstances.psoPrefixSumBuffer",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) {
						ExecuteHistogramAndPrefixSum(ctx, draw);
					}))
			.AddPass(
				PassDesc()
					.SetName(std::format("Compact Instances {}.{}", draw.drawIdx, draw.cullIdx))
					.AddBufferArg(
						"scene.instanceBuffer",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						"scene.instanceVisibility",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					// Only the visible instances are written, at offsets the prefix sum decides, so
					// a stale entry left over from the previous frame is a plausible draw.
					.AddPoisonedBufferArg(
						"scene.compactedInstances",
						BarrierSyncFlag::kComputeShader)
					.AddBufferArg(
						"compactedInstances.psoPrefixSumBuffer",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						"compactedInstances.compactDispatchArgs",
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

		draw.cullState->GetPsoPrefixSum().Clear(cmd);
		m_CullStats.Clear(cmd);

		cmd->WriteBuffer(
			draw.cullState->GetCullView().GetBufferHandle(),
			&draw.viewState.cullView,
			sizeof(idl::CullView));

		static constexpr std::array<idl::DispatchArgs, c_PsoCount> c_Seed = [] {
			std::array<idl::DispatchArgs, c_PsoCount> seed{};
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
		uniforms["cullView"]       = ctx.GetBuffer("cull.view");
		uniforms["instanceBuffer"] = ctx.GetBuffer("scene.instanceBuffer");
		uniforms["meshBuffer"]     = ctx.GetBuffer("scene.meshInstanceBuffer");
		uniforms["submeshBuffer"]  = ctx.GetBuffer("scene.submeshBuffer");
		uniforms["visibility"]     = ctx.GetBuffer("scene.instanceVisibility");

		// The stats writes are gated to BERNINI_GPU_DEBUG, so a release build drops the handle from
		// the kernel's reflection; bind it only when it survived.
		if (auto stats = uniforms["stats"]; stats.IsValid())
		{
			stats = ctx.GetBuffer("cull.stats");
		}

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

		auto instanceBuffer     = ctx.GetBuffer("scene.instanceBuffer");
		auto psoPrefixSumBuffer = ctx.GetBuffer("compactedInstances.psoPrefixSumBuffer");

		m_Histogram["gUniforms"]["instanceBuffer"] = instanceBuffer;
		m_Histogram["gUniforms"]["visibility"]     = ctx.GetBuffer("scene.instanceVisibility");

		// Reuse histogram buffer as prefix sum buffer
		m_Histogram["gUniforms"]["outBuffer"] = psoPrefixSumBuffer;

		auto cmdList = ctx.GetCommandList();

		auto computeState   = ComputeState();
		computeState.kernel = &m_Histogram;

		cmdList->SetComputeState(computeState);

		const auto instanceCount = draw.view->GetInstanceCount();
		cmdList->Dispatch(core::div_ceil(instanceCount, idl::cHistogramGroupSize), 1, 1);

		// The histogram writes psoPrefixSum (UAV); the prefix-sum scan below reads and
		// rewrites the same buffer. Both dispatches run back-to-back inside this single
		// frame-graph pass, so no pass-boundary barrier separates them -- insert an
		// explicit UAV barrier or the scan races the histogram. The race only corrupts
		// results with multiple PSO buckets (a lone bucket's base is the prefix sum of
		// prior, empty buckets, which is always 0), which is why it shows up as
		// flickering only in scenes mixing PSO types.
		cmdList->Barrier(
			psoPrefixSumBuffer,
			BufferBarrierDesc()
				.AddSyncBefore(BarrierSyncFlag::kComputeShader)
				.AddAccessBefore(BarrierAccessFlag::kUnorderedAccess)
				.AddSyncAfter(BarrierSyncFlag::kComputeShader)
				.AddAccessAfter(BarrierAccessFlag::kUnorderedAccess));

		m_PrefixSum["gUniforms"]["inOutBuffer"] = psoPrefixSumBuffer;

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

		auto instanceBuffer              = ctx.GetBuffer("scene.instanceBuffer");
		auto compactedInstancesBuffer    = ctx.GetBuffer("scene.compactedInstances");
		auto psoPrefixSumBuffer          = ctx.GetBuffer("compactedInstances.psoPrefixSumBuffer");
		auto compactedDispatchArgsBuffer = ctx.GetBuffer("compactedInstances.compactDispatchArgs");

		m_CompactInstances["gUniforms"]["instanceBuffer"] = instanceBuffer;
		m_CompactInstances["gUniforms"]["visibility"]   = ctx.GetBuffer("scene.instanceVisibility");
		m_CompactInstances["gUniforms"]["psoPrefixSum"] = psoPrefixSumBuffer;
		m_CompactInstances["gUniforms"]["compactedInstances"] = compactedInstancesBuffer;
		m_CompactInstances["gUniforms"]["dispatchArgs"]       = compactedDispatchArgsBuffer;

		auto cmdList = ctx.GetCommandList();

		auto computeState   = ComputeState();
		computeState.kernel = &m_CompactInstances;

		cmdList->SetComputeState(computeState);

		const auto instanceCount = draw.view->GetInstanceCount();
		cmdList->Dispatch(core::div_ceil(instanceCount, idl::cCompactGroupSize), 1, 1);
	}
}
