#include "gfx/GraphicsBase.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace
{
	// Two independently submitting contexts over one device: enough to exercise the fence timeline
	// and the GPU-side wait one queue takes on another.
	struct QueueFixture
	{
		bgl::GraphicsRef         gfx;
		bgpu::ResourceManagerRef resourceManager;
		bgpu::IDevice*           device = nullptr;

		bgpu::CommandQueueRef     queueA;
		bgpu::CommandQueueRef     queueB;
		bgpu::CommandAllocatorRef allocA;
		bgpu::CommandAllocatorRef allocB;
		bgpu::CommandListRef      listA;
		bgpu::CommandListRef      listB;

		QueueFixture()
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

			auto listDesc = bgpu::CommandListDesc();
			listDesc.type = bgpu::QueueType::kGraphics;

			queueA = device->CreateCommandQueue(bgpu::QueueType::kGraphics);
			queueB = device->CreateCommandQueue(bgpu::QueueType::kGraphics);
			allocA = device->CreateCommandAllocator();
			allocB = device->CreateCommandAllocator();
			listA  = device->CreateCommandList(listDesc, allocA, resourceManager);
			listB  = device->CreateCommandList(listDesc, allocB, resourceManager);

			resourceManager->RegisterQueue(queueA.Get());
			resourceManager->RegisterQueue(queueB.Get());
		}

		~QueueFixture()
		{
			queueA->Flush();
			queueB->Flush();
			resourceManager->UnregisterQueue(queueA.Get());
			resourceManager->UnregisterQueue(queueB.Get());
		}
	};

	bgpu::BufferHandle
	MakeBuffer(const bgpu::ResourceManagerRef& rm, uint32_t elements, const char* name)
	{
		auto desc         = bgpu::StructBufferDesc();
		desc.stride       = sizeof(uint32_t);
		desc.elementCount = elements;
		desc.isUav        = true;
		desc.debugName    = name;
		return rm->CreateStructBuffer(desc);
	}
}

TEST_CASE_METHOD(QueueFixture, "A queue's fence timeline advances and can be polled", "[queuesync]")
{
	const uint32_t           count  = 256;
	const bgpu::BufferHandle buffer = MakeBuffer(resourceManager, count, "Fence Timeline");
	REQUIRE_FALSE(buffer.IsNull());

	const std::vector<uint32_t> payload(count, 0x5Au);

	const uint64_t before = queueA->GetNextFenceValue();

	listA->Open(queueA.Get(), allocA.Get());
	listA->WriteBuffer(buffer, payload.data(), 0, payload.size() * sizeof(uint32_t));
	listA->Close();
	const uint64_t fence = queueA->ExecuteCommandList(listA.Get());

	// ExecuteCommandList hands back the value this submission will signal, and the queue moves past
	// it, so a later submission cannot be handed the same one.
	CHECK(fence == before);
	CHECK(queueA->GetNextFenceValue() > fence);

	queueA->WaitForFenceCPUBlocking(fence);

	CHECK(queueA->IsFenceComplete(fence));
	CHECK(queueA->PollCurrentFenceValue() >= fence);
	CHECK(queueA->GetLastCompletedFence() >= fence);

	// Nothing has been submitted at the next value, so it cannot have completed.
	CHECK_FALSE(queueA->IsFenceComplete(queueA->GetNextFenceValue()));

	resourceManager->DestroyBuffer(buffer);
}

TEST_CASE_METHOD(QueueFixture, "Flush drains everything already submitted", "[queuesync]")
{
	const uint32_t           count  = 1024;
	const bgpu::BufferHandle buffer = MakeBuffer(resourceManager, count, "Flush Drain");
	REQUIRE_FALSE(buffer.IsNull());

	const std::vector<uint32_t> payload(count, 7u);

	listA->Open(queueA.Get(), allocA.Get());
	listA->WriteBuffer(buffer, payload.data(), 0, payload.size() * sizeof(uint32_t));
	listA->Close();
	const uint64_t fence = queueA->ExecuteCommandList(listA.Get());

	queueA->Flush();

	CHECK(queueA->IsFenceComplete(fence));

	resourceManager->DestroyBuffer(buffer);
}

// The one that matters, and it has to be decisive rather than a race: B is made to wait on a fence
// value A has not reached yet, so an unencoded wait shows up as B completing early. Ordering by
// payload size does not test this -- a 4 MiB upload finished before B could lose the race, and the
// test passed with the wait deliberately removed.
TEST_CASE_METHOD(
	QueueFixture,
	"A GPU-side wait holds a queue until the other signals",
	"[queuesync]")
{
	const uint32_t           count  = 4096;
	const bgpu::BufferHandle source = MakeBuffer(resourceManager, count, "Cross-queue Source");
	REQUIRE_FALSE(source.IsNull());

	auto readbackDesc                         = bgpu::ReadbackBufferDesc();
	readbackDesc.byteSize                     = static_cast<uint64_t>(count) * sizeof(uint32_t);
	readbackDesc.debugName                    = "Cross-queue Readback";
	const bgpu::ReadbackBufferHandle readback = resourceManager->CreateReadbackBuffer(readbackDesc);
	REQUIRE_FALSE(readback.IsNull());

	const std::vector<uint32_t> payload(count, 0xC0FFEEu);

	// The value A's *next* submission will signal. Nothing has been submitted at it yet.
	const uint64_t futureA = queueA->GetNextFenceValue();
	REQUIRE_FALSE(queueA->IsFenceComplete(futureA));

	queueB->InsertWaitForQueueFence(queueA.Get(), futureA);

	listB->Open(queueB.Get(), allocB.Get());
	listB->CopyBufferToReadback(readback, source);
	listB->Close();
	const uint64_t fenceB = queueB->ExecuteCommandList(listB.Get());

	// Long enough that a copy this small would be done several times over if it were not blocked.
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK_FALSE(queueB->IsFenceComplete(fenceB));

	// Release it.
	listA->Open(queueA.Get(), allocA.Get());
	listA->WriteBuffer(source, payload.data(), 0, payload.size() * sizeof(uint32_t));
	listA->Close();
	const uint64_t fenceA = queueA->ExecuteCommandList(listA.Get());
	REQUIRE(fenceA == futureA);

	queueB->WaitForFenceCPUBlocking(fenceB);
	CHECK(queueB->IsFenceComplete(fenceB));

	// And the ordering held: B's copy saw A's write, not the buffer before it.
	const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(readback));
	REQUIRE(mapped != nullptr);
	CHECK(mapped[0] == 0xC0FFEEu);
	CHECK(mapped[count - 1] == 0xC0FFEEu);
	resourceManager->UnmapReadback(readback);

	resourceManager->DestroyReadbackBuffer(readback);
	resourceManager->DestroyBuffer(source);
}

TEST_CASE_METHOD(
	QueueFixture,
	"InsertWaitForQueue waits on everything submitted so far",
	"[queuesync]")
{
	const uint32_t           count  = 4096;
	const bgpu::BufferHandle source = MakeBuffer(resourceManager, count, "Wait-for-queue Source");
	REQUIRE_FALSE(source.IsNull());

	auto readbackDesc                         = bgpu::ReadbackBufferDesc();
	readbackDesc.byteSize                     = static_cast<uint64_t>(count) * sizeof(uint32_t);
	readbackDesc.debugName                    = "Wait-for-queue Readback";
	const bgpu::ReadbackBufferHandle readback = resourceManager->CreateReadbackBuffer(readbackDesc);
	REQUIRE_FALSE(readback.IsNull());

	const std::vector<uint32_t> payload(count, 0xABCDEFu);

	listA->Open(queueA.Get(), allocA.Get());
	listA->WriteBuffer(source, payload.data(), 0, payload.size() * sizeof(uint32_t));
	listA->Close();
	queueA->ExecuteCommandList(listA.Get());

	queueB->InsertWaitForQueue(queueA.Get());

	listB->Open(queueB.Get(), allocB.Get());
	listB->CopyBufferToReadback(readback, source);
	listB->Close();
	queueB->WaitForFenceCPUBlocking(queueB->ExecuteCommandList(listB.Get()));

	const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(readback));
	REQUIRE(mapped != nullptr);
	CHECK(mapped[0] == 0xABCDEFu);
	CHECK(mapped[count - 1] == 0xABCDEFu);
	resourceManager->UnmapReadback(readback);

	resourceManager->DestroyReadbackBuffer(readback);
	resourceManager->DestroyBuffer(source);
}

// The pools must be fixed-capacity, because the lock-free Get*/Valid* reads the RHI promises are
// sound only while slot storage never moves. Exhaustion is the observable half of that: a pool
// built with no capacity would grow instead, reallocating under any concurrent reader. Sized from
// ResourceManagerDesc, so a small limit here is the whole test.
TEST_CASE("A resource pool is bounded and reports exhaustion", "[resourcemanager]")
{
	auto opts                        = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer = false;
	opts.graphics.maxRtvs            = 4;

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);
	auto rm = gfxBase->GetResourceManagerCpy();

	auto texDesc                  = bgpu::TextureDesc();
	texDesc.width                 = 4;
	texDesc.height                = 4;
	texDesc.format                = bgpu::Format::RGBA8_UNORM;
	texDesc.usage                 = bgpu::TextureUsageFlag::kRenderTarget;
	texDesc.debugName             = "Pool Bound";
	const bgpu::TextureHandle tex = rm->CreateTexture(texDesc);
	REQUIRE(rm->ValidTextureHandle(tex));

	auto rtvDesc   = bgpu::RtvDesc();
	rtvDesc.format = bgpu::Format::RGBA8_UNORM;

	std::vector<bgpu::RtvHandle> rtvs;
	for (uint32_t i = 0; i < opts.graphics.maxRtvs; ++i)
	{
		const bgpu::RtvHandle rtv = rm->CreateRtv(tex, rtvDesc);
		CHECK_FALSE(rtv.IsNull());
		rtvs.push_back(rtv);
	}

	// One past the limit: a null handle, not a grown pool.
	CHECK(rm->CreateRtv(tex, rtvDesc).IsNull());

	for (const bgpu::RtvHandle& rtv : rtvs) rm->DestroyRtv(rtv, false);
	rm->DestroyTexture(tex, false);
}
