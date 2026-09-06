#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "debug/BufferPoisoner.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "types/Barrier.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace
{
	// A device, a queue, and a poisoner over the same resource manager the buffers under test come
	// from. Constructed in place, never returned by value: BufferPoisoner owns a GPU resource and
	// so is neither copyable nor movable.
	struct PoisonFixture
	{
		bgl::GraphicsRef         gfx;
		bgl::ResourceManagerRef  resourceManager;
		bgl::IDevice*            device = nullptr;
		bgl::CommandAllocatorRef cmdAllocator;
		bgl::CommandListRef      cmdList;
		bgl::CommandQueueRef     cmdQueue;
		bgl::BufferPoisoner      poisoner;

		PoisonFixture()
		{
			auto opts                     = bgl::GraphicsOptions();
			opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
			opts.enableDebugLayer         = true;
			opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

			gfx = bgl::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			auto* gfxBase = gfx->As<bgl::GraphicsBase>();
			REQUIRE(gfxBase != nullptr);

			resourceManager = gfxBase->GetResourceManagerCpy();
			device          = gfxBase->GetDevice();

			auto cmdListDesc = bgl::CommandListDesc();
			cmdListDesc.type = bgl::QueueType::kGraphics;

			cmdAllocator = device->CreateCommandAllocator();
			cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
			cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

			poisoner.Init(resourceManager);
		}

		~PoisonFixture() { poisoner.Release(false); }

		PoisonFixture(const PoisonFixture&) = delete;
		PoisonFixture(PoisonFixture&&)      = delete;

		PoisonFixture&
		operator=(const PoisonFixture&) = delete;

		PoisonFixture&
		operator=(PoisonFixture&&) = delete;
	};

	bgl::BufferBarrierDesc
	ToCopySource() noexcept
	{
		return bgl::BufferBarrierDesc()
		    .AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
		    .AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
		    .AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
		    .AddAccessAfter(bgl::BarrierAccessFlag::kCopySource);
	}
}

// The fill has to cover the whole buffer, including a buffer larger than the pattern chunk the
// poisoner tiles from -- the tail of a partially covered buffer is exactly where a stale value
// would survive, which is what poisoning exists to prevent.
TEST_CASE("Poisoning fills a buffer larger than the pattern chunk", "[poison][render]")
{
	PoisonFixture fixture;

	// Over 64 KiB, so the fill takes more than one copy and the last one is a partial chunk.
	constexpr uint32_t c_Count = 20'000;

	auto bufDesc = bgl::ComputeBufferDesc();
	bufDesc.SetElement<uint32_t>().SetInitialCount(c_Count).SetDebugName("Poison Target");

	auto target = fixture.resourceManager->CreateComputeBuffer(bufDesc);
	REQUIRE(fixture.resourceManager->ValidBufferHandle(target));

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = c_Count * sizeof(uint32_t);
	rbDesc.debugName = "Poison Readback";

	auto readback = fixture.resourceManager->CreateReadbackBuffer(rbDesc);

	fixture.cmdList->Open(fixture.cmdQueue, fixture.cmdAllocator);
	fixture.poisoner.Poison(fixture.cmdList, target);
	fixture.cmdList->Barrier(target, ToCopySource());
	fixture.cmdList->CopyBufferToReadback(readback, target);
	fixture.cmdList->Close();

	auto fence = fixture.cmdQueue->ExecuteCommandList(fixture.cmdList);
	fixture.cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* mapped =
		static_cast<const uint32_t*>(fixture.resourceManager->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	uint32_t unpoisoned = 0;
	for (uint32_t i = 0; i < c_Count; ++i)
	{
		if (mapped[i] != bgl::c_PoisonWord)
		{
			++unpoisoned;
		}
	}
	CHECK(unpoisoned == 0);

	fixture.resourceManager->UnmapReadback(readback);

	fixture.resourceManager->DestroyReadbackBuffer(readback, false);
	fixture.resourceManager->DestroyBuffer(target, false);
}

// Poison is only worth anything if what a dispatch writes replaces it and what the dispatch skips
// does not. A fresh buffer reads back as zeros on both backends, so a test that only checked the
// written half would pass with no poison at all -- the untouched tail is the assertion that counts.
TEST_CASE("A dispatch overwrites the poison it was given, and only that", "[poison][render]")
{
	PoisonFixture fixture;

	// CSComputeBufferTest runs one group of 8 threads, so the second half is never written.
	constexpr uint32_t c_Count   = 16;
	constexpr uint32_t c_Written = 8;

	auto bufDesc = bgl::ComputeBufferDesc();
	bufDesc.SetElement<uint32_t>().SetInitialCount(c_Count).SetDebugName("Poison Dispatch Target");

	auto target = fixture.resourceManager->CreateComputeBuffer(bufDesc);
	REQUIRE(fixture.resourceManager->ValidBufferHandle(target));

	auto kernel = fixture.device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(fixture.device->CreateShader("CSComputeBufferTest"))
			.SetDebugName("CSComputeBufferTest"));

	kernel["gUniforms"]["outBuffer"] = target;

	auto state   = bgl::ComputeState();
	state.kernel = &kernel;

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = c_Count * sizeof(uint32_t);
	rbDesc.debugName = "Poison Dispatch Readback";

	auto readback = fixture.resourceManager->CreateReadbackBuffer(rbDesc);

	fixture.cmdList->Open(fixture.cmdQueue, fixture.cmdAllocator);

	fixture.poisoner.Poison(fixture.cmdList, target);
	fixture.cmdList->Barrier(
		target,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgl::BarrierAccessFlag::kUnorderedAccess));

	fixture.cmdList->SetComputeState(state);
	fixture.cmdList->Dispatch(1, 1, 1);

	fixture.cmdList->Barrier(
		target,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));

	fixture.cmdList->CopyBufferToReadback(readback, target);
	fixture.cmdList->Close();

	auto fence = fixture.cmdQueue->ExecuteCommandList(fixture.cmdList);
	fixture.cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* mapped =
		static_cast<const uint32_t*>(fixture.resourceManager->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	for (uint32_t i = 0; i < c_Written; ++i)
	{
		CHECK(mapped[i] == i * 10u + 1u);
	}
	for (uint32_t i = c_Written; i < c_Count; ++i)
	{
		CHECK(mapped[i] == bgl::c_PoisonWord);
	}

	fixture.resourceManager->UnmapReadback(readback);

	fixture.resourceManager->DestroyReadbackBuffer(readback, false);
	fixture.resourceManager->DestroyBuffer(target, false);
}
