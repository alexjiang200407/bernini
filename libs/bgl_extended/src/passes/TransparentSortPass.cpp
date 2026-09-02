#include "passes/TransparentSortPass.h"
#include "fg/FrameGraph.h"
#include "passes/DrawData.h"
#include "pipeline/ComputePipeline.h"
#include "scene/scene_buffer_names.h"
#include <bgl/ISceneView.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/DispatchArgs.h>
#include <core/math.h>

namespace bgl
{
	void
	TransparentSortPass::Init(IDevice* device)
	{
		gassert(device != nullptr, "Device pointer is null");

		m_DepthKeys = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("programs.culling.TransparentDepthKeys"))
				.SetDebugName("Transparent Depth Keys"));

		m_Sort = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("programs.culling.TransparentSort"))
				.SetDebugName("Transparent Sort"));
	}

	void
	TransparentSortPass::Release()
	{
		logger::trace("TransparentSortPass::Release");

		m_DepthKeys.Reset();
		m_Sort.Reset();
	}

	void
	TransparentSortPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		fg.AddPass(
			  PassDesc()
				  .SetName("Transparent Sort Clear {}", draw.drawIdx)
				  .AddBufferArg(
					  c_TransparentSortCountName,
					  BarrierSyncFlag::kCopy,
					  BarrierAccessFlag::kCopyDest)
				  .AddBufferArg(
					  c_TransparentDispatchArgsName,
					  BarrierSyncFlag::kCopy,
					  BarrierAccessFlag::kCopyDest)
				  .SetExec([this](const PassContext& ctx) { ExecuteClear(ctx); }))
			.AddPass(
				PassDesc()
					.SetName("Transparent Depth Keys {}", draw.drawIdx)
					.AddBufferArg(
						c_InstanceBufferName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						c_MeshInstanceBufferName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						c_InstanceVisibilityName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					// Only the transparent instances take a slot, and the count that says how many
					// is written by this same pass -- so a leftover entry is indistinguishable
					// from one this frame produced.
					.AddPoisonedBufferArg(
						c_TransparentSortEntriesName,
						BarrierSyncFlag::kComputeShader)
					.AddBufferArg(
						c_TransparentSortCountName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) { ExecuteDepthKeys(ctx, draw); }))
			.AddPass(
				PassDesc()
					.SetName("Transparent Sort {}", draw.drawIdx)
					.AddBufferArg(
						c_TransparentSortEntriesName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						c_TransparentSortCountName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						c_SortedTransparentInstancesName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						c_TransparentDispatchArgsName,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) { ExecuteSort(ctx, draw); }));
	}

	void
	TransparentSortPass::ExecuteClear(const PassContext& ctx)
	{
		auto cmd = ctx.GetCommandList();

		static constexpr uint32_t c_Zero = 0;
		cmd->WriteBuffer(ctx.GetBuffer(c_TransparentSortCountName), &c_Zero, sizeof(c_Zero));

		// Seeded rather than zeroed: a frame with no transparent instances still has the forward pass
		// issue its indirect dispatch, and a zeroed y/z would be an invalid dispatch.
		static constexpr idl::DispatchArgs c_Seed = { 0u, 1u, 1u };

		cmd->WriteBuffer(ctx.GetBuffer(c_TransparentDispatchArgsName), &c_Seed, sizeof(c_Seed));
	}

	void
	TransparentSortPass::ExecuteDepthKeys(const PassContext& ctx, const DrawData& draw)
	{
		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		m_DepthKeys["gUniforms"]["instanceBuffer"] = ctx.GetBuffer(c_InstanceBufferName);
		m_DepthKeys["gUniforms"]["meshBuffer"]     = ctx.GetBuffer(c_MeshInstanceBufferName);
		m_DepthKeys["gUniforms"]["visibility"]     = ctx.GetBuffer(c_InstanceVisibilityName);
		m_DepthKeys["gUniforms"]["outEntries"]     = ctx.GetBuffer(c_TransparentSortEntriesName);
		m_DepthKeys["gUniforms"]["outCount"]       = ctx.GetBuffer(c_TransparentSortCountName);
		m_DepthKeys["gUniforms"]["cameraPos"]      = draw.viewState.cameraPos;

		auto cmdList = ctx.GetCommandList();

		auto computeState   = ComputeState();
		computeState.kernel = &m_DepthKeys;
		cmdList->SetComputeState(computeState);

		cmdList->Dispatch(
			core::div_ceil(draw.view->GetInstanceCount(), idl::cHistogramGroupSize),
			1,
			1);
	}

	void
	TransparentSortPass::ExecuteSort(const PassContext& ctx, const DrawData& draw)
	{
		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		m_Sort["gUniforms"]["entries"]         = ctx.GetBuffer(c_TransparentSortEntriesName);
		m_Sort["gUniforms"]["count"]           = ctx.GetBuffer(c_TransparentSortCountName);
		m_Sort["gUniforms"]["sortedInstances"] = ctx.GetBuffer(c_SortedTransparentInstancesName);
		m_Sort["gUniforms"]["dispatchArgs"]    = ctx.GetBuffer(c_TransparentDispatchArgsName);

		auto cmdList = ctx.GetCommandList();

		auto computeState   = ComputeState();
		computeState.kernel = &m_Sort;
		cmdList->SetComputeState(computeState);

		cmdList->Dispatch(1, 1, 1);
	}
}
