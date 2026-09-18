#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/PackedBuffer.h"
#include "types/Barrier.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "types/SubmeshInstance.h"
#include "uniforms/Uniforms.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <array>
#include <bgl/IGraphics.h>
#include <bgl_common/idl/Bucket.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/DispatchArgs.h>
#include <bgl_common/idl/InstanceVisibility.h>
#include <bgl_common/idl/idl.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <iterator>
#include <vector>

// Drives the whole counting sort -- histogram, scan, compaction -- through a real FrameGraph, with
// the same pass declarations CompactInstancesPass makes, and checks every instance landed inside its
// own PSO bucket.
//
// The scan and the compaction both declare bucketPrefixSum as a UAV, so the graph sees no state change
// between them. It must still barrier: the compaction reads the bases the scan writes, and without
// one the two dispatches overlap and the compaction scatters against a pre-scan prefix sum. Only a
// bucket whose base is non-zero can detect that -- a lone bucket's base is the sum of empty buckets
// before it, which is 0 either way -- so the instances below span three buckets.
TEST_CASE(
	"Compact instances: every instance lands in its own PSO bucket exactly once",
	"[compute][compact]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	constexpr uint32_t c_ActiveCount = 4000;
	constexpr uint32_t c_PaddedCount =
		((c_ActiveCount + bgl::idl::cHistogramGroupSize - 1) / bgl::idl::cHistogramGroupSize) *
		bgl::idl::cHistogramGroupSize;

	// Bucket 1's base is the (empty) bucket 0: 0 before the scan and 0 after. The other two are
	// the ones with something to get wrong, and the last sits at the top of the ceiling -- a
	// second lap of the 128-thread reservation stride -- so nothing may assume ids stop short.
	constexpr uint32_t c_Buckets[]   = { 1u, 130u, bgl::idl::cMaxBuckets - 1u };
	constexpr uint32_t c_BucketCount = static_cast<uint32_t>(std::size(c_Buckets));

	auto instanceBuffer = bgl::PackedBuffer<bgl::SubmeshInstance>();
	{
		auto desc         = bgl::PackedBufferDesc();
		desc.initialCount = c_PaddedCount;
		desc.debugName    = "Compact Instances";
		instanceBuffer.Init(desc, resourceManager);
	}

	// The bucket each instance index carries, so a compacted index can be checked against the
	// bucket it was filed under.
	std::vector<uint32_t>                       bucketOf(c_ActiveCount);
	std::array<uint32_t, bgl::idl::cMaxBuckets> expectedCount{};

	for (uint32_t i = 0; i < c_ActiveCount; ++i)
	{
		const uint32_t bucket = c_Buckets[i % c_BucketCount];

		// Any non-null mesh entry: offset 0 is the null one, so the first element past it will do.
		auto instance                = bgl::SubmeshInstance();
		instance.meshInstance.offset = 1;
		instance.submeshIndex        = 0u;
		instance.bucket              = bucket;
		instanceBuffer.Add(instance);

		bucketOf[i] = bucket;
		expectedCount[bucket] += 1;
	}
	for (uint32_t i = c_ActiveCount; i < c_PaddedCount; ++i)
	{
		// A default SubmeshInstance names no mesh; the shader skips it.
		instanceBuffer.Add(bgl::SubmeshInstance());
	}

	// Exclusive base of each bucket -- where the compaction should have put it.
	std::array<uint32_t, bgl::idl::cMaxBuckets> expectedBase{};
	uint32_t                                    running = 0;
	for (uint32_t p = 0; p < bgl::idl::cMaxBuckets; ++p)
	{
		expectedBase[p] = running;
		running += expectedCount[p];
	}

	const auto makeCompute = [&](auto element, uint32_t count, const char* name) {
		auto buffer = bgl::ComputeBuffer();
		auto desc   = bgl::ComputeBufferDesc();
		desc.SetElement<decltype(element)>();
		desc.initialCount = count;
		desc.debugName    = name;
		buffer.Init(desc, resourceManager);
		return buffer;
	};

	auto bucketPrefixSum = makeCompute(uint32_t{}, bgl::idl::cMaxBuckets, "Bucket Prefix Sum");
	auto dispatchArgs =
		makeCompute(bgl::idl::DispatchArgs{}, bgl::idl::cMaxBuckets, "Compacted Dispatch Args");
	auto compacted = makeCompute(uint32_t{}, c_PaddedCount, "Compacted Instances");

	// The histogram and compaction now gate on a per-instance visibility word the cull pass writes.
	// This test isolates the counting sort, so it stands in for a cull that passed everything: the
	// buffer is seeded all-visible. Frustum culling has its own test.
	auto visibility = makeCompute(bgl::idl::InstanceVisibility{}, c_PaddedCount, "Visibility");

	const auto makeKernel = [&](const char* module, const char* debugName) {
		auto kernel = device->CreateComputeKernel(
			bgl::ComputePipelineDesc()
				.SetShader(device->CreateShader(module))
				.SetDebugName(debugName));
		REQUIRE(kernel.pipeline != nullptr);
		return kernel;
	};

	auto histogram = makeKernel("programs.culling.HistogramInstances", "Histogram Instances");
	auto prefixSum = makeKernel("programs.culling.PrefixSumInstances", "Prefix-Sum Instances");
	auto compact   = makeKernel("programs.culling.CompactInstances", "Compact Instances");

	bgl::FrameGraph fg;
	fg.RegisterQueue("main", cmdQueue, cmdList);

	fg.ImportBuffer("instanceBuffer", instanceBuffer.GetBufferHandle());
	fg.ImportBuffer("bucketPrefixSum", bucketPrefixSum.GetBufferHandle());
	fg.ImportBuffer("dispatchArgs", dispatchArgs.GetBufferHandle());
	fg.ImportBuffer("compactedInstances", compacted.GetBufferHandle());
	fg.ImportBuffer("visibility", visibility.GetBufferHandle());

	// Pass declarations mirror CompactInstancesPass. Diverge from them and this test stops standing
	// in for the renderer.
	fg.AddPass(
		bgl::PassDesc()
			.SetName("Clear")
			.AddBufferArg(
				"instanceBuffer",
				bgl::BarrierSyncFlag::kCopy,
				bgl::BarrierAccessFlag::kCopyDest)
			.AddBufferArg(
				"bucketPrefixSum",
				bgl::BarrierSyncFlag::kCopy,
				bgl::BarrierAccessFlag::kCopyDest)
			.AddBufferArg(
				"dispatchArgs",
				bgl::BarrierSyncFlag::kCopy,
				bgl::BarrierAccessFlag::kCopyDest)
			.AddBufferArg(
				"compactedInstances",
				bgl::BarrierSyncFlag::kCopy,
				bgl::BarrierAccessFlag::kCopyDest)
			.AddBufferArg(
				"visibility",
				bgl::BarrierSyncFlag::kCopy,
				bgl::BarrierAccessFlag::kCopyDest)
			.SetExec([&](const bgl::PassContext& ctx) {
				auto* cmd = ctx.GetCommandList();
				instanceBuffer.Update(cmd);
				bucketPrefixSum.Clear(cmd);
				compacted.Clear(cmd);

				const std::vector<uint32_t> allVisible(c_PaddedCount, 1u);
				cmd->WriteBuffer(
					visibility.GetBufferHandle(),
					allVisible.data(),
					allVisible.size() * sizeof(uint32_t));

				std::array<bgl::idl::DispatchArgs, bgl::idl::cMaxBuckets> seed{};
				for (bgl::idl::DispatchArgs& args : seed)
				{
					args = { 0u, 1u, 1u };
				}
				cmd->WriteBuffer(dispatchArgs.GetBufferHandle(), seed.data(), sizeof(seed));
			}));

	fg.AddPass(
		bgl::PassDesc()
			.SetName("HistogramAndPrefixSum")
			.AddBufferArg(
				"instanceBuffer",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kShaderResource)
			.AddBufferArg(
				"bucketPrefixSum",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddBufferArg(
				"visibility",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kUnorderedAccess)
			.SetExec([&](const bgl::PassContext& ctx) {
				auto* cmd = ctx.GetCommandList();

				histogram["gUniforms"]["instanceBuffer"] = instanceBuffer.GetBufferHandle();
				histogram["gUniforms"]["visibility"]     = visibility.GetBufferHandle();
				histogram["gUniforms"]["outBuffer"]      = bucketPrefixSum.GetBufferHandle();

				auto state   = bgl::ComputeState();
				state.kernel = &histogram;
				cmd->SetComputeState(state);
				cmd->Dispatch(c_PaddedCount / bgl::idl::cHistogramGroupSize, 1, 1);

				// Both dispatches live in this one pass, so the graph cannot barrier between them.
				cmd->Barrier(
					bucketPrefixSum.GetBufferHandle(),
					bgl::BufferBarrierDesc()
						.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
						.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
						.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
						.AddAccessAfter(bgl::BarrierAccessFlag::kUnorderedAccess));

				prefixSum["gUniforms"]["inOutBuffer"] = bucketPrefixSum.GetBufferHandle();

				state.kernel = &prefixSum;
				cmd->SetComputeState(state);
				cmd->Dispatch(1, 1, 1);
			}));

	fg.AddPass(
		bgl::PassDesc()
			.SetName("Compact")
			.AddBufferArg(
				"instanceBuffer",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kShaderResource)
			.AddBufferArg(
				"bucketPrefixSum",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddBufferArg(
				"visibility",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddBufferArg(
				"compactedInstances",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddBufferArg(
				"dispatchArgs",
				bgl::BarrierSyncFlag::kComputeShader,
				bgl::BarrierAccessFlag::kUnorderedAccess)
			.SetExec([&](const bgl::PassContext& ctx) {
				auto* cmd = ctx.GetCommandList();

				compact["gUniforms"]["instanceBuffer"]     = instanceBuffer.GetBufferHandle();
				compact["gUniforms"]["visibility"]         = visibility.GetBufferHandle();
				compact["gUniforms"]["bucketPrefixSum"]    = bucketPrefixSum.GetBufferHandle();
				compact["gUniforms"]["compactedInstances"] = compacted.GetBufferHandle();
				compact["gUniforms"]["dispatchArgs"]       = dispatchArgs.GetBufferHandle();

				auto state   = bgl::ComputeState();
				state.kernel = &compact;
				cmd->SetComputeState(state);
				cmd->Dispatch(
					(c_ActiveCount + bgl::idl::cCompactGroupSize - 1) / bgl::idl::cCompactGroupSize,
					1,
					1);
			}));

	fg.Compile(resourceManager.Get());

	// The compaction reads the bases the scan wrote. Both declare bucketPrefixSum as a UAV, so this
	// barrier is the only thing separating the two dispatches -- assert on that buffer specifically,
	// not merely that the pass barriers something (it always transitions compactedInstances).
	{
		const bgl::PassBarriers& barriers = fg.BarriersFor("Compact");

		const bool barriersPrefixSum =
			std::ranges::any_of(barriers.bufferHandles, [&](bgl::BufferHandle handle) {
				return handle.slot.index == bucketPrefixSum.GetBufferHandle().slot.index;
			});

		CHECK(barriersPrefixSum);
	}

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = static_cast<uint64_t>(c_PaddedCount) * sizeof(uint32_t);
	rbDesc.debugName = "Compacted Readback";
	auto rbCompacted = resourceManager->CreateReadbackBuffer(rbDesc);

	rbDesc.byteSize  = static_cast<uint64_t>(bgl::idl::cMaxBuckets) * sizeof(uint32_t);
	rbDesc.debugName = "Prefix-Sum Readback";
	auto rbPrefixSum = resourceManager->CreateReadbackBuffer(rbDesc);

	rbDesc.byteSize = static_cast<uint64_t>(bgl::idl::cMaxBuckets) * sizeof(bgl::idl::DispatchArgs);
	rbDesc.debugName = "Dispatch Args Readback";
	auto rbArgs      = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	fg.Execute();

	const auto toCopySource = [](bgl::BarrierSyncFlag sync, bgl::BarrierAccessFlag access) {
		return bgl::BufferBarrierDesc()
		    .AddSyncBefore(sync)
		    .AddAccessBefore(access)
		    .AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
		    .AddAccessAfter(bgl::BarrierAccessFlag::kCopySource);
	};

	cmdList->Barrier(
		compacted.GetBufferHandle(),
		toCopySource(
			bgl::BarrierSyncFlag::kComputeShader,
			bgl::BarrierAccessFlag::kUnorderedAccess));
	cmdList->CopyBufferToReadback(rbCompacted, compacted.GetBufferHandle());

	cmdList->Barrier(
		bucketPrefixSum.GetBufferHandle(),
		toCopySource(
			bgl::BarrierSyncFlag::kComputeShader,
			bgl::BarrierAccessFlag::kUnorderedAccess));
	cmdList->CopyBufferToReadback(rbPrefixSum, bucketPrefixSum.GetBufferHandle());

	cmdList->Barrier(
		dispatchArgs.GetBufferHandle(),
		toCopySource(
			bgl::BarrierSyncFlag::kComputeShader,
			bgl::BarrierAccessFlag::kUnorderedAccess));
	cmdList->CopyBufferToReadback(rbArgs, dispatchArgs.GetBufferHandle());

	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* prefixSumOut =
		static_cast<const uint32_t*>(resourceManager->MapReadback(rbPrefixSum));
	REQUIRE(prefixSumOut != nullptr);
	for (uint32_t p = 0; p < bgl::idl::cMaxBuckets; ++p)
	{
		const uint32_t exclusive = (p == 0) ? 0u : prefixSumOut[p - 1];
		CHECK(exclusive == expectedBase[p]);
	}
	// The scan is inclusive, so the last row carries the full total.
	CHECK(prefixSumOut[bgl::idl::cMaxBuckets - 1] == c_ActiveCount);
	resourceManager->UnmapReadback(rbPrefixSum);

	// The reservation loop strides the whole ceiling (two laps of a 128-thread group), but only a
	// bucket something filled may touch its args -- every other row must still hold the
	// { 0, 1, 1 } seed.
	const auto* argsOut = static_cast<const uint32_t*>(resourceManager->MapReadback(rbArgs));
	REQUIRE(argsOut != nullptr);
	for (uint32_t p = 0; p < bgl::idl::cMaxBuckets; ++p)
	{
		INFO("bucket " << p);
		CHECK(argsOut[p * 3 + 0] == expectedCount[p]);
		CHECK(argsOut[p * 3 + 1] == 1u);
		CHECK(argsOut[p * 3 + 2] == 1u);
	}
	resourceManager->UnmapReadback(rbArgs);

	const auto* compactedOut =
		static_cast<const uint32_t*>(resourceManager->MapReadback(rbCompacted));
	REQUIRE(compactedOut != nullptr);

	// A racing compaction scatters against a pre-scan prefix sum: every bucket whose base should be
	// non-zero lands on top of an earlier one, so its slots hold foreign instances and its own are
	// nowhere.
	uint32_t misfiled = 0;
	for (uint32_t p = 0; p < bgl::idl::cMaxBuckets; ++p)
	{
		for (uint32_t slot = expectedBase[p]; slot < expectedBase[p] + expectedCount[p]; ++slot)
		{
			const uint32_t instanceIdx = compactedOut[slot];
			if (instanceIdx >= c_ActiveCount || bucketOf[instanceIdx] != p)
			{
				++misfiled;
			}
		}
	}
	CHECK(misfiled == 0);

	// The compaction reserves one contiguous run per bucket per thread group, so an error in that
	// arithmetic overlaps two groups' runs: one instance gets written twice and another is dropped.
	// Both sit in the right bucket, so the misfiled count above cannot see it. 4000 instances is 32
	// groups of 128, the last one partial, so the runs actually have to abut.
	std::vector<uint32_t> occurrences(c_ActiveCount, 0u);
	for (uint32_t p = 0; p < bgl::idl::cMaxBuckets; ++p)
	{
		for (uint32_t slot = expectedBase[p]; slot < expectedBase[p] + expectedCount[p]; ++slot)
		{
			const uint32_t instanceIdx = compactedOut[slot];
			if (instanceIdx < c_ActiveCount)
			{
				++occurrences[instanceIdx];
			}
		}
	}

	uint32_t notWrittenExactlyOnce = 0;
	for (const uint32_t seen : occurrences)
	{
		if (seen != 1u)
		{
			++notWrittenExactlyOnce;
		}
	}
	CHECK(notWrittenExactlyOnce == 0);

	resourceManager->UnmapReadback(rbCompacted);

	instanceBuffer.Release(false);
	bucketPrefixSum.Release(false);
	dispatchArgs.Release(false);
	compacted.Release(false);
	visibility.Release(false);
	resourceManager->DestroyReadbackBuffer(rbCompacted, false);
	resourceManager->DestroyReadbackBuffer(rbPrefixSum, false);
	resourceManager->DestroyReadbackBuffer(rbArgs, false);
}
