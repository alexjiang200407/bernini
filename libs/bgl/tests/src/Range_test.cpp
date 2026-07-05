#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "scene/RangeBuffer.h"
#include <bgl/IGraphics.h>

TEST_CASE("RangeBuffer", "[range][scene]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = true;
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

	cmdList->Close();
}
