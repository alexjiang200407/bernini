#include "passes/CompactInstancesPass.h"
#include "fg/FrameGraph.h"
#include "idl/DispatchArgs.h"
#include "passes/DrawData.h"
#include "pipeline/ComputePipeline.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
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

		m_Histogram = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(
					device->CreateShader("shaders/HistogramInstances.dxil", "HistogramInstances"))
				.SetDebugName("Histogram Instances"));

		m_PrefixSum = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(
					device->CreateShader("shaders/PrefixSumInstances.dxil", "PrefixSumInstances"))
				.SetDebugName("Prefix-Sum Instances"));

		m_CompactInstances = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(
					device->CreateShader("shaders/CompactInstances.dxil", "CompactInstances"))
				.SetDebugName("Compact Instances"));

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<uint32_t>().SetMaxCount(c_PsoCount).SetDebugName("Pso Prefix Sum");

			m_PsoPrefixSumBuffer.Init(desc, resourceManager);
		}

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<idl::DispatchArgs>()
				.SetMaxCount(c_PsoCount)
				.SetDebugName("Compacted Dispatch Args");

			m_CompactedDispatchArgs.Init(desc, resourceManager);
		}
	}

	void
	CompactInstancesPass::Release(uint64_t fenceVal, bool deferred)
	{
		logger::trace("CompactInstancesPass::Release");

		m_Histogram.Reset();
		m_PrefixSum.Reset();
		m_CompactInstances.Reset();

		m_CompactedDispatchArgs.Release(fenceVal, deferred);
		m_PsoPrefixSumBuffer.Release(fenceVal, deferred);
	}

	void
	CompactInstancesPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		fg.ImportGlobalBuffer(
			  "compactedInstances.psoPrefixSumBuffer",
			  m_PsoPrefixSumBuffer.GetBufferHandle())
			.ImportGlobalBuffer(
				"compactedInstances.compactDispatchArgs",
				m_CompactedDispatchArgs.GetBufferHandle())
			.AddPass(
				PassDesc()
					.SetName(std::format("Compact Instances Update {}", draw.drawIdx))
					.AddBufferArg(
						"compactedInstances.psoPrefixSumBuffer",
						BarrierSyncFlag::kCopy,
						BarrierAccessFlag::kCopyDest)
					.AddBufferArg(
						"compactedInstances.compactDispatchArgs",
						BarrierSyncFlag::kCopy,
						BarrierAccessFlag::kCopyDest)
					.SetExec([this](const PassContext& ctx) { ExecuteClear(ctx); }))
			.AddPass(
				PassDesc()
					.SetName(std::format("Histogram and Prefix Sum Instances {}", draw.drawIdx))
					.AddBufferArg(
						"scene.instanceBuffer",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						"compactedInstances.psoPrefixSumBuffer",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) {
						ExecuteHistogramAndPrefixSum(ctx, draw);
					}))
			.AddPass(
				PassDesc()
					.SetName(std::format("Compact Instances {}", draw.drawIdx))
					.AddBufferArg(
						"scene.instanceBuffer",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						"scene.compactedInstances",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					// psoPrefixSum is read through its UAV descriptor
					// (ComputeBuffer<uint>), so declare kUnorderedAccess to match the
					// descriptor rather than kShaderResource.
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
	CompactInstancesPass::ExecuteClear(const PassContext& ctx)
	{
		auto cmd = ctx.GetCommandList();

		m_PsoPrefixSumBuffer.Clear(cmd);
		static constexpr std::array<idl::DispatchArgs, c_PsoCount> kSeed = [] {
			std::array<idl::DispatchArgs, c_PsoCount> seed{};
			for (idl::DispatchArgs& args : seed)
			{
				args = { 0u, 1u, 1u };
			}
			return seed;
		}();

		cmd->WriteBuffer(m_CompactedDispatchArgs.GetBufferHandle(), kSeed.data(), sizeof(kSeed));
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

		// Reuse histogram buffer as prefix sum buffer
		m_Histogram["gUniforms"]["outBuffer"] = psoPrefixSumBuffer;

		auto cmdList = ctx.GetCommandList();

		auto computeState   = ComputeState();
		computeState.kernel = &m_Histogram;

		cmdList->SetComputeState(computeState);

		const auto instanceCount = draw.view->GetInstanceCount();
		cmdList->Dispatch(core::div_ceil(instanceCount, c_HistogramGroupSize), 1, 1);

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

		m_CompactInstances["gUniforms"]["instanceBuffer"]     = instanceBuffer;
		m_CompactInstances["gUniforms"]["psoPrefixSum"]       = psoPrefixSumBuffer;
		m_CompactInstances["gUniforms"]["compactedInstances"] = compactedInstancesBuffer;
		m_CompactInstances["gUniforms"]["dispatchArgs"]       = compactedDispatchArgsBuffer;

		auto cmdList = ctx.GetCommandList();

		auto computeState   = ComputeState();
		computeState.kernel = &m_CompactInstances;

		cmdList->SetComputeState(computeState);

		const auto instanceCount = draw.view->GetInstanceCount();
		cmdList->Dispatch(core::div_ceil(instanceCount, c_CompactGroupSize), 1, 1);
	}
}
