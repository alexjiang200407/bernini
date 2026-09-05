#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "cmd/TimestampHeap.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "types/Barrier.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// The RHI's timestamp query, below the frame graph: a span around a dispatch reads back a start
// before its end, and a span around nothing the GPU can sample says so rather than reporting a
// stale pair. Backend-agnostic -- D3D12 writes a query heap, Metal attaches a counter sample
// buffer to the encoder the dispatch opens.
TEST_CASE("A timed span brackets the work recorded inside it", "[timing]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.enablePixDebug           = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	auto device          = gfxBase->GetDevice();

	auto heap = device->CreateTimestampHeap(8);
	if (heap == nullptr)
	{
		SKIP("The device cannot sample a timestamp at a pass boundary");
	}
	REQUIRE(heap->GetCapacity() == 8);

	auto cmdListDesc = bgl::CommandListDesc();
	cmdListDesc.type = bgl::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	// Ticks per second is what turns a pair of slots into a duration, and it is the one number
	// here that a backend could plausibly report as unknown.
	const double frequency = cmdQueue->GetTimestampFrequency();
	CHECK(frequency > 0.0);

	auto bufDesc = bgl::ComputeBufferDesc();
	bufDesc.SetElement<uint32_t>().SetInitialCount(8).SetDebugName("Timing Out Buffer");
	auto outBuf = resourceManager->CreateComputeBuffer(bufDesc);

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSComputeBufferTest"))
			.SetDebugName("CSComputeBufferTest"));
	kernel["gUniforms"]["outBuffer"] = outBuf;

	auto state   = bgl::ComputeState();
	state.kernel = &kernel;

	cmdList->Open(cmdQueue, cmdAllocator);

	// Slots 0-1: a span with a dispatch in it.
	cmdList->BeginTiming(heap.Get(), 0, 1);
	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);
	const bool dispatchSampled = cmdList->EndTiming();

	// Slots 2-3: a span holding a barrier and nothing else. D3D12 samples it regardless; Metal has
	// no encoder to hang a sample on and must say so.
	cmdList->BeginTiming(heap.Get(), 2, 3);
	cmdList->Barrier(
		outBuf,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgl::BarrierAccessFlag::kUnorderedAccess));
	const bool barrierSampled = cmdList->EndTiming();

	cmdList->ResolveTimestamps(heap.Get(), 0, 4);
	cmdList->Close();

	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

	std::array<uint64_t, 4> ticks{};
	heap->Read(0, ticks);

	REQUIRE(dispatchSampled);
	CHECK(ticks[0] != bgl::ITimestampHeap::c_UnwrittenTimestamp);
	CHECK(ticks[1] != bgl::ITimestampHeap::c_UnwrittenTimestamp);
	CHECK(ticks[1] >= ticks[0]);

	if (barrierSampled)
	{
		CHECK(ticks[2] != bgl::ITimestampHeap::c_UnwrittenTimestamp);
		CHECK(ticks[3] >= ticks[2]);
	}
	else
	{
		CHECK(ticks[2] == bgl::ITimestampHeap::c_UnwrittenTimestamp);
		CHECK(ticks[3] == bgl::ITimestampHeap::c_UnwrittenTimestamp);
	}

	// A dispatch of one threadgroup is microseconds, not a frame: a duration past a second means
	// the ticks were not converted, or the two slots came from different work.
	const double seconds = static_cast<double>(ticks[1] - ticks[0]) / frequency;
	CHECK(seconds < 1.0);

	// A span whose slots were never sampled must not leave a stale pair behind either.
	std::array<uint64_t, 2> untouched{};
	heap->Read(6, untouched);
	CHECK(untouched[0] == bgl::ITimestampHeap::c_UnwrittenTimestamp);
	CHECK(untouched[1] == bgl::ITimestampHeap::c_UnwrittenTimestamp);

	cmdQueue->Flush();
	resourceManager->DestroyBuffer(outBuf, false);
}
