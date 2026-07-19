#include "passes/TransparentSortPass.h"
#include "fg/FrameGraph.h"
#include "idl/Constants.h"
#include "idl/DispatchArgs.h"
#include "passes/DrawData.h"
#include "pipeline/ComputePipeline.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include <bgl/ISceneView.h>
#include <core/math.h>

namespace bgl
{
	namespace
	{
		// Owned by the SceneView: both are sized off its instance buffer, so the depth-key pass
		// cannot append past the end however many instances turn out to be transparent.
		constexpr auto c_EntriesBuffer     = "scene.transparentSortEntries";
		constexpr auto c_CountBuffer       = "scene.transparentSortCount";
		constexpr auto c_PartitionBase     = "transparentSort.partitionBase";
		constexpr auto c_PartitionArgs     = "transparentSort.partitionDispatchArgs";
		constexpr auto c_SortedTransparent = "scene.sortedTransparentInstances";
		constexpr auto c_InstanceBuffer    = "scene.instanceBuffer";
		constexpr auto c_MeshBuffer        = "scene.meshInstanceBuffer";
	}

	void
	TransparentSortPass::Init(IDevice* device, core::SharedRef<IResourceManager> resourceManager)
	{
		gassert(device != nullptr, "Device pointer is null");

		m_DepthKeys = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("TransparentDepthKeys"))
				.SetDebugName("Transparent Depth Keys"));

		m_Sort = device->CreateComputeKernel(
			ComputePipelineDesc()
				.SetShader(device->CreateShader("TransparentSort"))
				.SetDebugName("Transparent Sort"));

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<uint32_t>()
				.SetMaxCount(idl::cTransparentPartitionCount)
				.SetDebugName("Transparent Partition Base");

			m_PartitionBase.Init(desc, resourceManager);
		}

		{
			auto desc = ComputeBufferDesc();
			desc.SetElement<idl::DispatchArgs>()
				.SetMaxCount(idl::cTransparentPartitionCount)
				.SetDebugName("Transparent Partition Dispatch Args");

			m_PartitionDispatchArgs.Init(desc, resourceManager);
		}
	}

	void
	TransparentSortPass::Release(uint64_t fenceVal, bool deferred)
	{
		logger::trace("TransparentSortPass::Release");

		m_DepthKeys.Reset();
		m_Sort.Reset();

		m_PartitionDispatchArgs.Release(fenceVal, deferred);
		m_PartitionBase.Release(fenceVal, deferred);
	}

	void
	TransparentSortPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		fg.ImportGlobalBuffer(c_PartitionBase, m_PartitionBase.GetBufferHandle())
			.ImportGlobalBuffer(c_PartitionArgs, m_PartitionDispatchArgs.GetBufferHandle())
			.AddPass(
				PassDesc()
					.SetName(std::format("Transparent Sort Clear {}", draw.drawIdx))
					.AddBufferArg(
						c_CountBuffer,
						BarrierSyncFlag::kCopy,
						BarrierAccessFlag::kCopyDest)
					.AddBufferArg(
						c_PartitionArgs,
						BarrierSyncFlag::kCopy,
						BarrierAccessFlag::kCopyDest)
					.SetExec([this](const PassContext& ctx) { ExecuteClear(ctx); }))
			.AddPass(
				PassDesc()
					.SetName(std::format("Transparent Depth Keys {}", draw.drawIdx))
					.AddBufferArg(
						c_InstanceBuffer,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						c_MeshBuffer,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kShaderResource)
					.AddBufferArg(
						c_EntriesBuffer,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						c_CountBuffer,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) { ExecuteDepthKeys(ctx, draw); }))
			.AddPass(
				PassDesc()
					.SetName(std::format("Transparent Sort {}", draw.drawIdx))
					.AddBufferArg(
						c_EntriesBuffer,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						c_CountBuffer,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						c_SortedTransparent,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						c_PartitionBase,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.AddBufferArg(
						c_PartitionArgs,
						BarrierSyncFlag::kComputeShader,
						BarrierAccessFlag::kUnorderedAccess)
					.SetExec([draw, this](const PassContext& ctx) { ExecuteSort(ctx, draw); }));
	}

	void
	TransparentSortPass::ExecuteClear(const PassContext& ctx)
	{
		auto cmd = ctx.GetCommandList();

		static constexpr uint32_t c_Zero = 0;
		cmd->WriteBuffer(ctx.GetBuffer(c_CountBuffer), &c_Zero, sizeof(c_Zero));

		// Seeded rather than zeroed: a frame with no transparent instances still has the forward
		// pass issue its three indirect dispatches, and a zeroed y/z would be an invalid dispatch.
		static constexpr std::array<idl::DispatchArgs, idl::cTransparentPartitionCount> c_Seed =
			[] {
				std::array<idl::DispatchArgs, idl::cTransparentPartitionCount> seed{};
				for (idl::DispatchArgs& args : seed)
				{
					args = { 0u, 1u, 1u };
				}
				return seed;
			}();

		cmd->WriteBuffer(m_PartitionDispatchArgs.GetBufferHandle(), c_Seed.data(), sizeof(c_Seed));
	}

	void
	TransparentSortPass::ExecuteDepthKeys(const PassContext& ctx, const DrawData& draw)
	{
		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		m_DepthKeys["gUniforms"]["instanceBuffer"] = ctx.GetBuffer(c_InstanceBuffer);
		m_DepthKeys["gUniforms"]["meshBuffer"]     = ctx.GetBuffer(c_MeshBuffer);
		m_DepthKeys["gUniforms"]["outEntries"]     = ctx.GetBuffer(c_EntriesBuffer);
		m_DepthKeys["gUniforms"]["outCount"]       = ctx.GetBuffer(c_CountBuffer);
		m_DepthKeys["gUniforms"]["cameraPos"]      = draw.cameraPos;

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

		m_Sort["gUniforms"]["entries"]               = ctx.GetBuffer(c_EntriesBuffer);
		m_Sort["gUniforms"]["count"]                 = ctx.GetBuffer(c_CountBuffer);
		m_Sort["gUniforms"]["sortedInstances"]       = ctx.GetBuffer(c_SortedTransparent);
		m_Sort["gUniforms"]["partitionBase"]         = ctx.GetBuffer(c_PartitionBase);
		m_Sort["gUniforms"]["partitionDispatchArgs"] = ctx.GetBuffer(c_PartitionArgs);

		auto cmdList = ctx.GetCommandList();

		auto computeState   = ComputeState();
		computeState.kernel = &m_Sort;
		cmdList->SetComputeState(computeState);

		cmdList->Dispatch(1, 1, 1);
	}
}
