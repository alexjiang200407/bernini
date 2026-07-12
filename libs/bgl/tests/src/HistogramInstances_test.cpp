#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "idl/idl.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "scene/EntryBuffer.h"
#include "scene/PackedBuffer.h"
#include "scene/RangeBuffer.h"
#include "types/ComputeState.h"
#include "types/SubmeshInstance.h"
#include "uniforms/Uniforms.h"
#include <bgl/IGraphics.h>
#include <bgl/PsoType.h>

// Exercises the counting-sort front end on the GPU: HistogramInstances counts instances
// per PSO type, PrefixSumInstances turns that histogram into an inclusive prefix sum.
// Enough instances are used (spanning several thread groups, many sharing a bucket, plus
// padding slots) that a non-atomic histogram or a racy scan would give the wrong answer.
TEST_CASE("Bucket instances: histogram then prefix sum", "[compute][histogram][prefixsum]")
{
	// Must match HistogramInstances.slang's [numthreads].
	constexpr uint32_t kThreadsPerGroup = 256;

	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = true;

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

	// Many real instances spread (with collisions) across the non-null PSO types. The
	// instance buffer is padded to a whole number of groups; the histogram dispatches one
	// thread per slot and skips the kInvalid padding, so no instance count is passed.
	constexpr uint32_t     activeCount = 1000;
	constexpr uint32_t     groupCount  = (activeCount + kThreadsPerGroup - 1) / kThreadsPerGroup;
	constexpr uint32_t     paddedCount = groupCount * kThreadsPerGroup;
	constexpr bgl::PsoType buckets[]   = { bgl::PsoType::kOpaque_StaticMesh_PBR,
		                                   bgl::PsoType::kAlphaTest_StaticMesh_PBR,
		                                   bgl::PsoType::kTransparent_StaticMesh_PBR };

	// The PSO bucket is resolved onto the instance, so the histogram reads nothing else -- no mesh,
	// no submesh, no indirection to build. Two instances of one submesh are free to bucket
	// differently, which is exactly what a per-instance material override is.
	constexpr uint32_t kBucketCount = static_cast<uint32_t>(std::size(buckets));

	auto instanceBuffer = bgl::PackedBuffer<bgl::SubmeshInstance>();
	{
		auto desc      = bgl::PackedBufferDesc();
		desc.maxCount  = paddedCount;
		desc.debugName = "Histogram Instances";
		instanceBuffer.Init(desc, resourceManager);
	}

	const auto addInstance = [&](uint32_t bucketIdx) {
		auto instance = bgl::SubmeshInstance();

		// Any non-null mesh entry: the shader only tests it against the padding sentinel.
		instance.meshInstance.offset = 0u;
		instance.submeshIndex        = 0u;
		instance.pso                 = static_cast<uint32_t>(buckets[bucketIdx]);

		instanceBuffer.Add(instance);
	};

	std::array<uint32_t, bgl::c_PsoCount> expectedHistogram{};
	for (uint32_t i = 0; i < activeCount; ++i)
	{
		const uint32_t b = i % kBucketCount;
		addInstance(b);
		expectedHistogram[static_cast<size_t>(buckets[b])] += 1;
	}
	// Padding slots carry a null meshInstance sentinel; the shader skips them.
	for (uint32_t i = activeCount; i < paddedCount; ++i)
	{
		auto instance                = bgl::SubmeshInstance();
		instance.meshInstance.offset = 0xFFFFFFFFu;
		instanceBuffer.Add(instance);
	}

	std::array<uint32_t, bgl::c_PsoCount> expectedPrefixSum{};
	uint32_t                              running = 0;
	for (uint32_t i = 0; i < bgl::c_PsoCount; ++i)
	{
		running += expectedHistogram[i];
		expectedPrefixSum[i] = running;  // inclusive scan
	}

	auto outBuffer = bgl::ComputeBuffer();
	{
		auto desc = bgl::ComputeBufferDesc();
		desc.SetElement<uint32_t>();
		desc.maxCount  = bgl::c_PsoCount;
		desc.debugName = "Histogram Output";
		outBuffer.Init(desc, resourceManager);
	}

	auto histogramKernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("HistogramInstances"))
			.SetDebugName("Histogram Instances"));
	REQUIRE(histogramKernel.pipeline != nullptr);

	auto prefixSumKernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("PrefixSumInstances"))
			.SetDebugName("Prefix-Sum Instances"));
	REQUIRE(prefixSumKernel.pipeline != nullptr);

	// ComputeBuffer wraps its UAV handle in a struct, so it binds through the same
	// smart-buffer path (by BufferHandle) as the read-only instance buffer.
	histogramKernel["gUniforms"]["instanceBuffer"] = instanceBuffer.GetBufferHandle();
	histogramKernel["gUniforms"]["outBuffer"]      = outBuffer.GetBufferHandle();

	prefixSumKernel["gUniforms"]["inOutBuffer"] = outBuffer.GetBufferHandle();

	const auto makeReadback = [&](const char* name) {
		auto desc      = bgl::ReadbackBufferDesc();
		desc.byteSize  = static_cast<uint64_t>(bgl::c_PsoCount) * sizeof(uint32_t);
		desc.debugName = name;
		return resourceManager->CreateReadbackBuffer(desc);
	};
	auto rbHistogram = makeReadback("Histogram Readback");
	auto rbPrefixSum = makeReadback("Prefix-Sum Readback");

	const auto bufferBarrier = [](bgl::BarrierSyncFlag   syncBefore,
	                              bgl::BarrierAccessFlag accessBefore,
	                              bgl::BarrierSyncFlag   syncAfter,
	                              bgl::BarrierAccessFlag accessAfter) {
		return bgl::BufferBarrierDesc()
		    .AddSyncBefore(syncBefore)
		    .AddAccessBefore(accessBefore)
		    .AddSyncAfter(syncAfter)
		    .AddAccessAfter(accessAfter);
	};

	cmdList->Open(cmdQueue, cmdAllocator);

	// Upload the instances and zero the histogram. The instance buffer is the sort's only input.
	instanceBuffer.Update(cmdList);
	outBuffer.Clear(cmdList);

	cmdList->Barrier(
		instanceBuffer.GetBufferHandle(),
		bufferBarrier(
			bgl::BarrierSyncFlag::kCopy,
			bgl::BarrierAccessFlag::kCopyDest,
			bgl::BarrierSyncFlag::kComputeShader,
			bgl::BarrierAccessFlag::kShaderResource));
	cmdList->Barrier(
		outBuffer.GetBufferHandle(),
		bufferBarrier(
			bgl::BarrierSyncFlag::kCopy,
			bgl::BarrierAccessFlag::kCopyDest,
			bgl::BarrierSyncFlag::kComputeShader,
			bgl::BarrierAccessFlag::kUnorderedAccess));

	// Histogram: one thread per (padded) instance slot.
	auto histogramState   = bgl::ComputeState();
	histogramState.kernel = &histogramKernel;
	cmdList->SetComputeState(histogramState);
	cmdList->Dispatch(groupCount, 1, 1);

	// Snapshot the histogram, then hand the buffer back to the prefix-sum pass.
	cmdList->Barrier(
		outBuffer.GetBufferHandle(),
		bufferBarrier(
			bgl::BarrierSyncFlag::kComputeShader,
			bgl::BarrierAccessFlag::kUnorderedAccess,
			bgl::BarrierSyncFlag::kCopy,
			bgl::BarrierAccessFlag::kCopySource));
	cmdList->CopyBufferToReadback(rbHistogram, outBuffer.GetBufferHandle());
	cmdList->Barrier(
		outBuffer.GetBufferHandle(),
		bufferBarrier(
			bgl::BarrierSyncFlag::kCopy,
			bgl::BarrierAccessFlag::kCopySource,
			bgl::BarrierSyncFlag::kComputeShader,
			bgl::BarrierAccessFlag::kUnorderedAccess));

	// Prefix sum (in place, single group over the buckets).
	auto prefixState   = bgl::ComputeState();
	prefixState.kernel = &prefixSumKernel;
	cmdList->SetComputeState(prefixState);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		outBuffer.GetBufferHandle(),
		bufferBarrier(
			bgl::BarrierSyncFlag::kComputeShader,
			bgl::BarrierAccessFlag::kUnorderedAccess,
			bgl::BarrierSyncFlag::kCopy,
			bgl::BarrierAccessFlag::kCopySource));
	cmdList->CopyBufferToReadback(rbPrefixSum, outBuffer.GetBufferHandle());

	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* histogram = static_cast<const uint32_t*>(resourceManager->MapReadback(rbHistogram));
	REQUIRE(histogram != nullptr);
	for (uint32_t i = 0; i < bgl::c_PsoCount; ++i)
	{
		CHECK(histogram[i] == expectedHistogram[i]);
	}
	resourceManager->UnmapReadback(rbHistogram);

	const auto* prefixSum = static_cast<const uint32_t*>(resourceManager->MapReadback(rbPrefixSum));
	REQUIRE(prefixSum != nullptr);
	for (uint32_t i = 0; i < bgl::c_PsoCount; ++i)
	{
		CHECK(prefixSum[i] == expectedPrefixSum[i]);
	}
	resourceManager->UnmapReadback(rbPrefixSum);

	outBuffer.Release(fence, false);
	instanceBuffer.Release(fence, false);
	resourceManager->DestroyReadbackBuffer(rbHistogram, fence, false);
	resourceManager->DestroyReadbackBuffer(rbPrefixSum, fence, false);
}
