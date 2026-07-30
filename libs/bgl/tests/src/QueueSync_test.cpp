#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

namespace
{
	// Two independently submitting contexts over one device: enough to exercise the fence timeline
	// and the GPU-side wait one queue takes on another.
	struct QueueFixture
	{
		bgl::GraphicsRef        gfx;
		bgl::ResourceManagerRef resourceManager;
		bgl::IDevice*           device = nullptr;

		bgl::CommandQueueRef     queueA;
		bgl::CommandQueueRef     queueB;
		bgl::CommandAllocatorRef allocA;
		bgl::CommandAllocatorRef allocB;
		bgl::CommandListRef      listA;
		bgl::CommandListRef      listB;

		QueueFixture()
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

			auto listDesc = bgl::CommandListDesc();
			listDesc.type = bgl::QueueType::kGraphics;

			queueA = device->CreateCommandQueue(bgl::QueueType::kGraphics);
			queueB = device->CreateCommandQueue(bgl::QueueType::kGraphics);
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

	bgl::BufferHandle
	MakeBuffer(const bgl::ResourceManagerRef& rm, uint32_t elements, const char* name)
	{
		auto desc         = bgl::StructBufferDesc();
		desc.stride       = sizeof(uint32_t);
		desc.elementCount = elements;
		desc.isUav        = true;
		desc.debugName    = name;
		return rm->CreateStructBuffer(desc);
	}
}

TEST_CASE_METHOD(QueueFixture, "A queue's fence timeline advances and can be polled", "[queuesync]")
{
	const uint32_t          count  = 256;
	const bgl::BufferHandle buffer = MakeBuffer(resourceManager, count, "Fence Timeline");
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
	const uint32_t          count  = 1024;
	const bgl::BufferHandle buffer = MakeBuffer(resourceManager, count, "Flush Drain");
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
	const uint32_t          count  = 4096;
	const bgl::BufferHandle source = MakeBuffer(resourceManager, count, "Cross-queue Source");
	REQUIRE_FALSE(source.IsNull());

	auto readbackDesc                        = bgl::ReadbackBufferDesc();
	readbackDesc.byteSize                    = static_cast<uint64_t>(count) * sizeof(uint32_t);
	readbackDesc.debugName                   = "Cross-queue Readback";
	const bgl::ReadbackBufferHandle readback = resourceManager->CreateReadbackBuffer(readbackDesc);
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
	const uint32_t          count  = 4096;
	const bgl::BufferHandle source = MakeBuffer(resourceManager, count, "Wait-for-queue Source");
	REQUIRE_FALSE(source.IsNull());

	auto readbackDesc                        = bgl::ReadbackBufferDesc();
	readbackDesc.byteSize                    = static_cast<uint64_t>(count) * sizeof(uint32_t);
	readbackDesc.debugName                   = "Wait-for-queue Readback";
	const bgl::ReadbackBufferHandle readback = resourceManager->CreateReadbackBuffer(readbackDesc);
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
