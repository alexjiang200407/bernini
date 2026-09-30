#include "debug/BufferPoisoner.h"
#include "gfx/GraphicsBase.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace
{
	// A device, a queue, and a poisoner over the same resource manager the buffers under test come
	// from. Constructed in place, never returned by value: BufferPoisoner owns a GPU resource and
	// so is neither copyable nor movable.
	struct PoisonFixture
	{
		bgl::GraphicsRef          gfx;
		bgpu::ResourceManagerRef  resourceManager;
		bgpu::IDevice*            device = nullptr;
		bgpu::CommandAllocatorRef cmdAllocator;
		bgpu::CommandListRef      cmdList;
		bgpu::CommandQueueRef     cmdQueue;
		bgl::BufferPoisoner       poisoner;

		PoisonFixture()
		{
			auto opts                                = bgl::test::GraphicsSetup();
			opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
			opts.gpuContext.enableDebugLayer         = true;
			opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

			gfx = bgl::test::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			auto* gfxBase = gfx->As<bgl::GraphicsBase>();
			REQUIRE(gfxBase != nullptr);

			resourceManager = gfxBase->GetResourceManagerCpy();
			device          = gfxBase->GetDevice();

			auto cmdListDesc = bgpu::CommandListDesc();
			cmdListDesc.type = bgpu::QueueType::kGraphics;

			cmdAllocator = device->CreateCommandAllocator();
			cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
			cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

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

	bgpu::BufferBarrierDesc
	ToCopySource() noexcept
	{
		return bgpu::BufferBarrierDesc()
		    .AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
		    .AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
		    .AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
		    .AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource);
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

	auto bufDesc = bgpu::ComputeBufferDesc();
	bufDesc.SetElement<uint32_t>().SetInitialCount(c_Count).SetDebugName("Poison Target");

	auto target = fixture.resourceManager->CreateComputeBuffer(bufDesc);
	REQUIRE(fixture.resourceManager->ValidBufferHandle(target));

	auto rbDesc      = bgpu::ReadbackBufferDesc();
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

	auto bufDesc = bgpu::ComputeBufferDesc();
	bufDesc.SetElement<uint32_t>().SetInitialCount(c_Count).SetDebugName("Poison Dispatch Target");

	auto target = fixture.resourceManager->CreateComputeBuffer(bufDesc);
	REQUIRE(fixture.resourceManager->ValidBufferHandle(target));

	auto kernel = fixture.device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(fixture.device->CreateShader("CSComputeBufferTest"))
			.SetDebugName("CSComputeBufferTest"));

	kernel["gUniforms"]["outBuffer"] = target;

	auto state   = bgpu::ComputeState();
	state.kernel = &kernel;

	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = c_Count * sizeof(uint32_t);
	rbDesc.debugName = "Poison Dispatch Readback";

	auto readback = fixture.resourceManager->CreateReadbackBuffer(rbDesc);

	fixture.cmdList->Open(fixture.cmdQueue, fixture.cmdAllocator);

	fixture.poisoner.Poison(fixture.cmdList, target);
	fixture.cmdList->Barrier(
		target,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kUnorderedAccess));

	fixture.cmdList->SetComputeState(state);
	fixture.cmdList->Dispatch(1, 1, 1);

	fixture.cmdList->Barrier(
		target,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));

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
