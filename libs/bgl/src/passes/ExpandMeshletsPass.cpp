#include "passes/ExpandMeshletsPass.h"
#include "fg/FrameGraph.h"
#include "idl/BaseTable.h"
#include "idl/Constants.h"
#include "idl/DrawIndirectArgs.h"
#include "passes/DrawData.h"
#include "pipeline/ComputePipeline.h"
#include "resource/ResourceManager.h"
#include "util/util.h"
#include <bgl/ISceneView.h>
#include <bgl/PsoType.h>
#include <core/math.h>

namespace bgl
{
	void
	ExpandMeshletsPass::Init(IDevice* device, core::SharedRef<IResourceManager> resourceManager)
	{
		gassert(device != nullptr, "Device pointer is null");

		m_Histogram = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("HistogramMeshlets"))
				.SetDebugName("Histogram Meshlets"));

		m_PrefixSum = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("PrefixSumInstances"))
				.SetDebugName("Prefix-Sum Meshlets"));

		m_DrawArgs = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("MeshletDrawArgs"))
				.SetDebugName("Meshlet Draw Args"));

		m_Expand = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("ExpandMeshlets"))
				.SetDebugName("Expand Meshlets"));

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<uint32_t>()
				.SetInitialCount(c_PsoCount)
				.SetDebugName("Meshlet Prefix Sum");

			m_MeshletPrefixSum.Init(desc, resourceManager);
		}

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<idl::DrawIndirectArgs>()
				.SetInitialCount(c_PsoCount)
				.SetDebugName("Meshlet Draw Args");

			m_DrawArgsBuffer.Init(desc, resourceManager);
		}
	}

	void
	ExpandMeshletsPass::Release(bool deferred)
	{
		logger::trace("ExpandMeshletsPass::Release");

		m_Histogram.Reset();
		m_PrefixSum.Reset();
		m_DrawArgs.Reset();
		m_Expand.Reset();

		m_MeshletPrefixSum.Release(deferred);
		m_DrawArgsBuffer.Release(deferred);
	}

	void
	ExpandMeshletsPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		fg.ImportGlobalBuffer("expand.meshletPrefixSum", m_MeshletPrefixSum.GetBufferHandle())
			.ImportGlobalBuffer("expand.drawArgs", m_DrawArgsBuffer.GetBufferHandle())
			.AddPass(
				PassDesc()
					.SetName(std::format("Histogram Meshlets {}", draw.drawIdx))
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
						"scene.instanceVisibility",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						"expand.meshletPrefixSum",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) {
						ExecuteHistogramAndScan(ctx, draw);
					}))
			.AddPass(
				PassDesc()
					.SetName(std::format("Meshlet Draw Args {}", draw.drawIdx))
					.AddBufferArg(
						"expand.meshletPrefixSum",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						"expand.drawArgs",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) { ExecuteDrawArgs(ctx, draw); }))
			.AddPass(
				PassDesc()
					.SetName(std::format("Expand Meshlets {}", draw.drawIdx))
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
						"scene.compactedInstances",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						"compactedInstances.psoPrefixSumBuffer",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						"compactedInstances.compactDispatchArgs",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						"transparentSort.partitionBase",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						"scene.meshletInstances",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						"expand.drawArgs",
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) { ExecuteExpand(ctx, draw); }));
	}

	void
	ExpandMeshletsPass::ExecuteHistogramAndScan(const PassContext& ctx, const DrawData& draw)
	{
		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		auto cmd = ctx.GetCommandList();

		auto prefixSum = ctx.GetBuffer("expand.meshletPrefixSum");

		m_MeshletPrefixSum.Clear(cmd);

		Uniforms& uniforms         = m_Histogram["gUniforms"];
		uniforms["instanceBuffer"] = ctx.GetBuffer("scene.instanceBuffer");
		uniforms["meshBuffer"]     = ctx.GetBuffer("scene.meshInstanceBuffer");
		uniforms["submeshBuffer"]  = ctx.GetBuffer("scene.submeshBuffer");
		uniforms["visibility"]     = ctx.GetBuffer("scene.instanceVisibility");
		uniforms["outBuffer"]      = prefixSum;

		auto computeState   = ComputeState();
		computeState.kernel = &m_Histogram;
		cmd->SetComputeState(computeState);

		const auto instanceCount = draw.view->GetInstanceCount();
		cmd->Dispatch(core::div_ceil(instanceCount, idl::cHistogramGroupSize), 1, 1);

		// Histogram and scan share this pass, so no pass-boundary barrier separates them -- same
		// hazard CompactInstancesPass documents on its instance scan.
		cmd->Barrier(
			prefixSum,
			BufferBarrierDesc()
				.AddSyncBefore(BarrierSyncFlag::kComputeShader)
				.AddAccessBefore(BarrierAccessFlag::kUnorderedAccess)
				.AddSyncAfter(BarrierSyncFlag::kComputeShader)
				.AddAccessAfter(BarrierAccessFlag::kUnorderedAccess));

		m_PrefixSum["gUniforms"]["inOutBuffer"] = prefixSum;

		computeState.kernel = &m_PrefixSum;
		cmd->SetComputeState(computeState);
		cmd->Dispatch(1, 1, 1);
	}

	void
	ExpandMeshletsPass::ExecuteDrawArgs(const PassContext& ctx, const DrawData& draw)
	{
		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		m_DrawArgs["gUniforms"]["meshletPrefixSum"] = ctx.GetBuffer("expand.meshletPrefixSum");
		m_DrawArgs["gUniforms"]["drawArgs"]         = ctx.GetBuffer("expand.drawArgs");

		auto computeState   = ComputeState();
		computeState.kernel = &m_DrawArgs;

		auto cmd = ctx.GetCommandList();
		cmd->SetComputeState(computeState);
		cmd->Dispatch(1, 1, 1);
	}

	void
	ExpandMeshletsPass::ExecuteExpand(const PassContext& ctx, const DrawData& draw)
	{
		const auto instanceCount = draw.view->GetInstanceCount();
		if (instanceCount == 0)
		{
			return;
		}

		auto cmd = ctx.GetCommandList();

		Uniforms& expand           = m_Expand["gExpand"];
		expand["instanceBuffer"]   = ctx.GetBuffer("scene.instanceBuffer");
		expand["meshBuffer"]       = ctx.GetBuffer("scene.meshInstanceBuffer");
		expand["submeshBuffer"]    = ctx.GetBuffer("scene.submeshBuffer");
		expand["meshletInstances"] = ctx.GetBuffer("scene.meshletInstances");
		expand["drawArgs"]         = ctx.GetBuffer("expand.drawArgs");
		expand["dispatchArgs"]     = ctx.GetBuffer("compactedInstances.compactDispatchArgs");

		Uniforms& expansionData             = m_Expand["expansionData"];
		expansionData["compactedInstances"] = ctx.GetBuffer("scene.compactedInstances");
		expansionData["psoPrefixSum"] = ctx.GetBuffer("compactedInstances.psoPrefixSumBuffer");
		expansionData["transparentPartitionBase"] = ctx.GetBuffer("transparentSort.partitionBase");
		expansionData["baseTable"]                = idl::BaseTable::kPsoBucketed;

		auto computeState   = ComputeState();
		computeState.kernel = &m_Expand;

		// The transparent buckets expand with the depth-sorted draws in W4; until then only the
		// PSO-bucketed ones are covered. Each bucket's dispatch is sized for every instance and the
		// kernel exits past its bucket's own count -- the same CPU-sized over-dispatch the cull
		// kernels use, traded against an indirect-dispatch seam the RHI does not have yet.
		const uint32_t groups = core::div_ceil(instanceCount, idl::cCompactGroupSize);

		for (uint16_t pso = 0; pso < c_PsoCount; ++pso)
		{
			if (IsTransparentPso(pso))
			{
				continue;
			}

			expansionData["psoIndex"] = static_cast<uint32_t>(pso);

			cmd->SetComputeState(computeState);
			cmd->Dispatch(groups, 1, 1);
		}
	}
}
