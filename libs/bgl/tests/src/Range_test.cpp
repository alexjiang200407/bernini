#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/RangeBuffer.h"
#include "util/GpuValidation.h"
#include <bgl/IGraphics.h>

TEST_CASE("RangeBuffer", "[range][scene]")
{
	auto opts                     = bgl::GraphicsOptions();
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
		auto desc      = bgl::RangeBufferDesc();
		desc.maxCount  = 8;
		desc.blockSize = sizeof(int);  // One element per block.
		desc.debugName = "RangeBuffer Allocate";

		auto rb = bgl::RangeBuffer<int>(desc, resourceManager);

		auto handle = rb.AllocateRange(3);
		CHECK_FALSE(handle.is_null());
		CHECK(handle.index == 0);
		CHECK(handle.count == 3);
		CHECK(handle.generation == 0);

		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 3);
		CHECK(blockDirty(rb.GetDirtyBlocks(), 0));
		CHECK(blockDirty(rb.GetDirtyBlocks(), 1));
		CHECK(blockDirty(rb.GetDirtyBlocks(), 2));
		CHECK_FALSE(blockDirty(rb.GetDirtyBlocks(), 3));

		// A second allocation continues after the first range.
		auto handle2 = rb.AllocateRange(2);
		CHECK(handle2.index == 3);
		CHECK(handle2.count == 2);

		rb.Update(cmdList);
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 0);

		// Updating again with nothing dirty is a no-op.
		rb.Update(cmdList);
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 0);
	}

	SECTION("Add writes an element span and marks dirty")
	{
		auto desc      = bgl::RangeBufferDesc();
		desc.maxCount  = 8;
		desc.blockSize = sizeof(int);
		desc.debugName = "RangeBuffer Add";

		auto rb = bgl::RangeBuffer<int>(desc, resourceManager);

		const int values[] = { 10, 20, 30 };
		auto      handle   = rb.Add(std::span<const int>(values, 3));

		CHECK(handle.index == 0);
		CHECK(handle.count == 3);
		CHECK(rb.Get(handle, 0) == 10);
		CHECK(rb.Get(handle, 1) == 20);
		CHECK(rb.Get(handle, 2) == 30);
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 3);
	}

	SECTION("Set updates a single element and dirties one block")
	{
		auto desc      = bgl::RangeBufferDesc();
		desc.maxCount  = 8;
		desc.blockSize = sizeof(int);
		desc.debugName = "RangeBuffer Set";

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
		CHECK(blockDirty(rb.GetDirtyBlocks(), 1));
		CHECK_FALSE(blockDirty(rb.GetDirtyBlocks(), 0));
	}

	SECTION("Erase frees the range and reallocation bumps generation")
	{
		auto desc      = bgl::RangeBufferDesc();
		desc.maxCount  = 8;
		desc.blockSize = sizeof(int);
		desc.debugName = "RangeBuffer Erase";

		auto rb = bgl::RangeBuffer<int>(desc, resourceManager);

		auto handle = rb.AllocateRange(4);
		CHECK(handle.index == 0);
		CHECK(handle.generation == 0);

		rb.Update(cmdList);

		rb.Erase(handle);
		// Erase marks the freed range dirty so the GPU copy reflects the removal.
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 4);

		// The coalesced free space is reused, with a bumped generation.
		auto reused = rb.AllocateRange(4);
		CHECK(reused.index == 0);
		CHECK(reused.count == 4);
		CHECK(reused.generation == 1);
	}

	SECTION("Dirty tracking spans multiple blocks")
	{
		auto desc      = bgl::RangeBufferDesc();
		desc.maxCount  = 8;
		desc.blockSize = 4 * sizeof(int);  // Four elements per block => 2 blocks.
		desc.debugName = "RangeBuffer Spanning";

		auto rb = bgl::RangeBuffer<int>(desc, resourceManager);

		// A 5-element range straddles block 0 (elems 0-3) and block 1 (elem 4).
		auto handle = rb.AllocateRange(5);
		CHECK(handle.count == 5);
		CHECK(dirtyCount(rb.GetDirtyBlocks()) == 2);
		CHECK(blockDirty(rb.GetDirtyBlocks(), 0));
		CHECK(blockDirty(rb.GetDirtyBlocks(), 1));
	}

	SECTION("IsValid detects use-after-free and EraseByIndex frees ranges")
	{
		auto desc      = bgl::RangeBufferDesc();
		desc.maxCount  = 8;
		desc.blockSize = sizeof(int);
		desc.debugName = "RangeBuffer IsValid";

		auto rb = bgl::RangeBuffer<int>(desc, resourceManager);

		auto handle = rb.AllocateRange(3);
		CHECK(rb.IsValid(handle));

		rb.Erase(handle);
		CHECK_FALSE(rb.IsValid(handle));

		// Reallocating reuses index 0 with a bumped generation; the old handle
		// stays invalid while the new one is valid.
		auto reused = rb.AllocateRange(3);
		CHECK(reused.index == 0);
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
		auto desc      = bgl::RangeBufferDesc();
		desc.maxCount  = 16;
		desc.blockSize = 4 * sizeof(uint32_t);  // Four elements per block => 4 blocks.
		desc.debugName = "RangeBuffer Offset Upload";

		auto rb = bgl::RangeBuffer<uint32_t>(desc, resourceManager);

		// Fill blocks 0-1 and flush, so the next upload's dirty run cannot start at block 0.
		const uint32_t low[]     = { 100, 101, 102, 103, 104, 105, 106, 107 };
		auto           lowHandle = rb.Add(std::span<const uint32_t>(low, std::size(low)));
		rb.Update(cmdList);

		const uint32_t high[]     = { 200, 201, 202, 203 };
		auto           highHandle = rb.Add(std::span<const uint32_t>(high, std::size(high)));
		REQUIRE(highHandle.index == 8);  // Entirely inside block 2.
		rb.Update(cmdList);

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = desc.maxCount * sizeof(uint32_t);
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
		resourceManager->DestroyReadbackBuffer(readback, fence, false);
		rb.Release(fence, false);

		// The case-wide Close below expects an open list.
		cmdList->Open(cmdQueue, cmdAllocator);
	}

	cmdList->Close();
}
