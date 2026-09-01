#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/RangeBuffer.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

TEST_CASE("RangeBuffer", "[range][scene]")
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

	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc = bgl::CommandListDesc();
	cmdListDesc.type = bgl::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);

	auto cmdQueue = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	cmdList->Open(cmdQueue, cmdAllocator);

	const auto dirtyCount = [](const std::vector<bool>& blocks) {
		return static_cast<uint32_t>(std::count(blocks.begin(), blocks.end(), true));
	};

	const auto blockDirty = [](const std::vector<bool>& blocks, size_t i) {
		return i < blocks.size() && static_cast<bool>(blocks[i]);
	};

	SECTION("AllocateRange marks dirty blocks")
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 8;
		desc.blockSize    = sizeof(int);  // One element per block.
		desc.debugName    = "RangeBuffer Allocate";

		auto rb = bgl::RangeBuffer<int>(desc, resourceManager);

		// Construction reserves element 0 and leaves its block dirty; flushing it leaves the counts
		// below the caller's writes alone.
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 1);
		CHECK(blockDirty(rb.GetDirtyBlocks(), 0));
		rb.Update(cmdList);

		auto handle = rb.AllocateRange(3);
		CHECK_FALSE(handle.is_null());
		CHECK(handle.index == 1);
		CHECK(handle.count == 3);
		CHECK(handle.generation == 0);

		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 3);
		CHECK(blockDirty(rb.GetDirtyBlocks(), 1));
		CHECK(blockDirty(rb.GetDirtyBlocks(), 2));
		CHECK(blockDirty(rb.GetDirtyBlocks(), 3));
		CHECK_FALSE(blockDirty(rb.GetDirtyBlocks(), 4));

		// A second allocation continues after the first range.
		auto handle2 = rb.AllocateRange(2);
		CHECK(handle2.index == 4);
		CHECK(handle2.count == 2);

		rb.Update(cmdList);
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 0);

		// Updating again with nothing dirty is a no-op.
		rb.Update(cmdList);
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 0);
	}

	SECTION("Add writes an element span and marks dirty")
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 8;
		desc.blockSize    = sizeof(int);
		desc.debugName    = "RangeBuffer Add";

		auto rb = bgl::RangeBuffer<int>(desc, resourceManager);
		rb.Update(cmdList);  // Flushes the reserved null element.

		const int values[] = { 10, 20, 30 };
		auto      handle   = rb.Add(std::span<const int>(values, 3));

		CHECK(handle.index == 1);
		CHECK(handle.count == 3);
		CHECK(rb.Get(handle, 0) == 10);
		CHECK(rb.Get(handle, 1) == 20);
		CHECK(rb.Get(handle, 2) == 30);
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 3);
	}

	SECTION("Set updates a single element and dirties one block")
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 8;
		desc.blockSize    = sizeof(int);
		desc.debugName    = "RangeBuffer Set";

		auto rb = bgl::RangeBuffer<int>(desc, resourceManager);

		const int values[] = { 1, 2, 3 };
		auto      handle   = rb.Add(std::span<const int>(values, 3));

		rb.Update(cmdList);
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 0);

		rb.Set(handle, 1, 99);
		CHECK(rb.Get(handle, 0) == 1);
		CHECK(rb.Get(handle, 1) == 99);
		CHECK(rb.Get(handle, 2) == 3);

		// Only the touched element's block is dirty.
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 1);
		CHECK(blockDirty(rb.GetDirtyBlocks(), 2));
		CHECK_FALSE(blockDirty(rb.GetDirtyBlocks(), 1));
	}

	SECTION("Erase frees the range and reallocation bumps generation")
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 8;
		desc.blockSize    = sizeof(int);
		desc.debugName    = "RangeBuffer Erase";

		auto rb = bgl::RangeBuffer<int>(desc, resourceManager);

		auto handle = rb.AllocateRange(4);
		CHECK(handle.index == 1);
		CHECK(handle.generation == 0);

		rb.Update(cmdList);

		rb.Erase(handle);
		// Erase marks the freed range dirty so the GPU copy reflects the removal.
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 4);

		// The coalesced free space is reused, with a bumped generation.
		auto reused = rb.AllocateRange(4);
		CHECK(reused.index == 1);
		CHECK(reused.count == 4);
		CHECK(reused.generation == 1);
	}

	SECTION("Dirty tracking spans multiple blocks")
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 8;
		desc.blockSize    = 4 * sizeof(int);  // Four elements per block => 2 blocks.
		desc.debugName    = "RangeBuffer Spanning";

		auto rb = bgl::RangeBuffer<int>(desc, resourceManager);
		rb.Update(cmdList);  // Flushes the reserved null element.

		// A 5-element range straddles block 0 (elems 1-3) and block 1 (elems 4-5).
		auto handle = rb.AllocateRange(5);
		CHECK(handle.count == 5);
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 2);
		CHECK(blockDirty(rb.GetDirtyBlocks(), 0));
		CHECK(blockDirty(rb.GetDirtyBlocks(), 1));
	}

	SECTION("IsValid detects use-after-free and EraseByIndex frees ranges")
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 8;
		desc.blockSize    = sizeof(int);
		desc.debugName    = "RangeBuffer IsValid";

		auto rb = bgl::RangeBuffer<int>(desc, resourceManager);

		// The reserved element is not a live range, so a null offset read back from a GPU-side
		// struct resolves to nothing rather than to element 0.
		CHECK_FALSE(rb.IsIndexValid(0));

		auto handle = rb.AllocateRange(3);
		CHECK(handle.index != 0);
		CHECK(rb.IsValid(handle));
		CHECK(rb.IsIndexValid(handle.index));

		rb.Erase(handle);
		CHECK_FALSE(rb.IsValid(handle));

		// Reallocating reuses index 1 with a bumped generation; the old handle
		// stays invalid while the new one is valid.
		auto reused = rb.AllocateRange(3);
		CHECK(reused.index == 1);
		CHECK(reused.generation == handle.generation + 1);
		CHECK(rb.IsValid(reused));
		CHECK_FALSE(rb.IsValid(handle));

		// EraseByIndex frees a range by its starting index (as DeleteGeom does).
		rb.EraseByIndex(reused.index);
		CHECK_FALSE(rb.IsValid(reused));

		CHECK_FALSE(rb.IsValid(core::multi_slot_handle{}));
	}

	// Regression: IssueCopy used to source every upload from the mirror's base, so a dirty run
	// past block 0 uploaded the mirror's FIRST bytes into a LATER GPU region. Invisible until
	// something allocates beyond the first block -- e.g. a thumbnail sphere added after a large
	// mesh -- whose GPU data then belonged to another range entirely.
	SECTION("A dirty range past the first block uploads its own bytes")
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 16;
		desc.blockSize    = 4 * sizeof(uint32_t);  // Four elements per block => 4 blocks.
		desc.debugName    = "RangeBuffer Offset Upload";

		auto rb = bgl::RangeBuffer<uint32_t>(desc, resourceManager);

		// Fill blocks 0-1 and flush, so the next upload's dirty run cannot start at block 0.
		const uint32_t low[]     = { 100, 101, 102, 103, 104, 105, 106, 107 };
		auto           lowHandle = rb.Add(std::span<const uint32_t>(low, std::size(low)));
		rb.Update(cmdList);

		const uint32_t high[]     = { 200, 201, 202, 203 };
		auto           highHandle = rb.Add(std::span<const uint32_t>(high, std::size(high)));
		REQUIRE(highHandle.index == 9);  // Entirely inside block 2.
		rb.Update(cmdList);

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = static_cast<uint64_t>(rb.Capacity()) * sizeof(uint32_t);
		rbDesc.debugName = "RangeBuffer Offset Upload Readback";
		auto readback    = resourceManager->CreateReadbackBuffer(rbDesc);

		auto barrier = bgl::BufferBarrierDesc();
		barrier.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource);
		cmdList->Barrier(rb.GetBufferHandle(), barrier);

		cmdList->CopyBufferToReadback(readback, rb.GetBufferHandle());
		cmdList->Close();

		auto fence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(fence);

		const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(readback));
		REQUIRE(mapped != nullptr);

		for (uint32_t i = 0; i < std::size(low); ++i)
		{
			CHECK(mapped[lowHandle.index + i] == low[i]);
		}
		for (uint32_t i = 0; i < std::size(high); ++i)
		{
			CHECK(mapped[highHandle.index + i] == high[i]);
		}

		resourceManager->UnmapReadback(readback);
		resourceManager->DestroyReadbackBuffer(readback, false);
		rb.Release(false);

		// The case-wide Close below expects an open list.
		cmdList->Open(cmdQueue, cmdAllocator);
	}

	SECTION("A range that outgrows the buffer is served rather than refused")
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 4;
		desc.blockSize    = sizeof(uint32_t);
		desc.debugName    = "RangeBuffer Grow";

		auto rb = bgl::RangeBuffer<uint32_t>(desc, resourceManager);

		// initialCount is the caller's budget; the reserved null element rides on top of it.
		REQUIRE(rb.Capacity() == desc.initialCount + 1);

		const uint32_t first[]     = { 10, 20, 30, 40 };
		auto           firstHandle = rb.Add(std::span<const uint32_t>(first, std::size(first)));
		REQUIRE(rb.Capacity() == desc.initialCount + 1);

		// One past the initial ceiling: the old contract threw here.
		const uint32_t second[]     = { 50, 60 };
		auto           secondHandle = rb.Add(std::span<const uint32_t>(second, std::size(second)));

		CHECK(rb.Capacity() >= 7);
		CHECK(rb.IsValid(firstHandle));
		CHECK(rb.IsValid(secondHandle));

		// The pre-growth handle still addresses the same slots -- growth must not renumber.
		CHECK(firstHandle.index == 1);

		rb.Release(false);
	}

	// The whole point of copy-on-grow. The first range is uploaded into the original resource, which
	// growth then replaces; only the forward copy recorded by the next Update carries it into the new
	// one. Without that copy this reads back zeroes.
	SECTION("Data uploaded before a growth survives it")
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 4;
		desc.blockSize    = sizeof(uint32_t);
		desc.debugName    = "RangeBuffer Grow Preserve";

		auto rb = bgl::RangeBuffer<uint32_t>(desc, resourceManager);

		const uint32_t before[]     = { 111, 222, 333, 444 };
		auto           beforeHandle = rb.Add(std::span<const uint32_t>(before, std::size(before)));

		// Flushed into the original resource, and the mirror is clean afterwards, so nothing would
		// re-upload these words later.
		rb.Update(cmdList);
		cmdList->Close();
		auto uploadFence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(uploadFence);
		cmdAllocator->ResetAllocator();
		cmdList->Open(cmdQueue, cmdAllocator);

		const uint32_t after[]     = { 555, 666 };
		auto           afterHandle = rb.Add(std::span<const uint32_t>(after, std::size(after)));
		REQUIRE(rb.Capacity() >= 6);

		rb.Update(cmdList);

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = static_cast<uint64_t>(rb.Capacity()) * sizeof(uint32_t);
		rbDesc.debugName = "RangeBuffer Grow Preserve Readback";
		auto readback    = resourceManager->CreateReadbackBuffer(rbDesc);

		auto barrier = bgl::BufferBarrierDesc();
		barrier.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource);
		cmdList->Barrier(rb.GetBufferHandle(), barrier);

		cmdList->CopyBufferToReadback(readback, rb.GetBufferHandle());
		cmdList->Close();

		auto fence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(fence);

		const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(readback));
		REQUIRE(mapped != nullptr);

		for (uint32_t i = 0; i < std::size(before); ++i)
		{
			CHECK(mapped[beforeHandle.index + i] == before[i]);
		}
		for (uint32_t i = 0; i < std::size(after); ++i)
		{
			CHECK(mapped[afterHandle.index + i] == after[i]);
		}

		resourceManager->UnmapReadback(readback);
		resourceManager->DestroyReadbackBuffer(readback, false);
		rb.Release(false);

		cmdAllocator->ResetAllocator();
		cmdList->Open(cmdQueue, cmdAllocator);
	}

	// Two growths with no Update between them: the middle resource never receives the forward copy,
	// so a naive "copy from the one I just replaced" reads uninitialised memory.
	SECTION("Data survives two growths with no flush between them")
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 2;
		desc.blockSize    = sizeof(uint32_t);
		desc.debugName    = "RangeBuffer Double Grow";

		auto rb = bgl::RangeBuffer<uint32_t>(desc, resourceManager);

		const uint32_t seed[]     = { 7, 8 };
		auto           seedHandle = rb.Add(std::span<const uint32_t>(seed, std::size(seed)));

		rb.Update(cmdList);
		cmdList->Close();
		auto seedFence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(seedFence);
		cmdAllocator->ResetAllocator();
		cmdList->Open(cmdQueue, cmdAllocator);

		const uint32_t more[]  = { 9 };
		const uint32_t more2[] = { 11 };
		rb.Add(std::span<const uint32_t>(more, std::size(more)));
		rb.Add(std::span<const uint32_t>(more2, std::size(more2)));

		rb.Update(cmdList);

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = static_cast<uint64_t>(rb.Capacity()) * sizeof(uint32_t);
		rbDesc.debugName = "RangeBuffer Double Grow Readback";
		auto readback    = resourceManager->CreateReadbackBuffer(rbDesc);

		auto barrier = bgl::BufferBarrierDesc();
		barrier.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource);
		cmdList->Barrier(rb.GetBufferHandle(), barrier);

		cmdList->CopyBufferToReadback(readback, rb.GetBufferHandle());
		cmdList->Close();

		auto fence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(fence);

		const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(readback));
		REQUIRE(mapped != nullptr);

		for (uint32_t i = 0; i < std::size(seed); ++i)
		{
			CHECK(mapped[seedHandle.index + i] == seed[i]);
		}

		resourceManager->UnmapReadback(readback);
		resourceManager->DestroyReadbackBuffer(readback, false);
		rb.Release(false);

		cmdAllocator->ResetAllocator();
		cmdList->Open(cmdQueue, cmdAllocator);
	}

	// The forward copy on growth writes [0, oldBytes) of the new resource, and the same Update then
	// uploads dirty ranges into it -- including data added below the old capacity but never flushed,
	// so the old resource the copy reads is stale there. The two writes overlap; only a barrier
	// between them makes the dirty upload win. Without it the copied (stale) bytes can survive.
	SECTION("Dirty data below the old capacity survives a growth in the same Update")
	{
		auto desc         = bgl::RangeBufferDesc();
		desc.initialCount = 4;
		desc.blockSize    = sizeof(uint32_t);
		desc.debugName    = "RangeBuffer Grow Overlap";

		auto rb = bgl::RangeBuffer<uint32_t>(desc, resourceManager);

		// Fits the initial capacity, dirties slots below it, and is NOT flushed -- so the original
		// resource never receives these bytes.
		const uint32_t low[]     = { 111, 222 };
		auto           lowHandle = rb.Add(std::span<const uint32_t>(low, std::size(low)));

		// Forces the growth while low[] is still pending. The forward copy's [0, 20B) region covers
		// lowHandle's slots.
		const uint32_t high[]     = { 333, 444, 555, 666 };
		auto           highHandle = rb.Add(std::span<const uint32_t>(high, std::size(high)));
		REQUIRE(rb.Capacity() >= 6);

		rb.Update(cmdList);

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = static_cast<uint64_t>(rb.Capacity()) * sizeof(uint32_t);
		rbDesc.debugName = "RangeBuffer Grow Overlap Readback";
		auto readback    = resourceManager->CreateReadbackBuffer(rbDesc);

		auto barrier = bgl::BufferBarrierDesc();
		barrier.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource);
		cmdList->Barrier(rb.GetBufferHandle(), barrier);

		cmdList->CopyBufferToReadback(readback, rb.GetBufferHandle());
		cmdList->Close();

		auto fence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(fence);

		const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(readback));
		REQUIRE(mapped != nullptr);

		CHECK(mapped[lowHandle.index + 0] == low[0]);
		CHECK(mapped[lowHandle.index + 1] == low[1]);
		for (uint32_t i = 0; i < std::size(high); ++i)
		{
			CHECK(mapped[highHandle.index + i] == high[i]);
		}

		resourceManager->UnmapReadback(readback);
		resourceManager->DestroyReadbackBuffer(readback, false);
		rb.Release(false);

		cmdAllocator->ResetAllocator();
		cmdList->Open(cmdQueue, cmdAllocator);
	}

	cmdList->Close();
}
