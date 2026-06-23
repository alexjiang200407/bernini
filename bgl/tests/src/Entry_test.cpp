#include "cmd/CommandAllocator.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "scene/EntryBuffer.h"
#include <bgl/IGraphics.h>

TEST_CASE("EntryBuffer", "[entry][scene]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = true;
	opts.headless                 = true;
	opts.height                   = 1;
	opts.width                    = 1;
	opts.wnd                      = nullptr;
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

	SECTION("CRUD")
	{
		auto desc      = bgl::EntryBufferDesc();
		desc.maxCount  = 3;
		desc.blockSize = sizeof(int);
		desc.debugName = "Test Entry Buffer";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		core::slot_handle handles[3];

		handles[0] = entryBuffer.EmplaceBack(1);
		REQUIRE(entryBuffer.CountDirtyBlocks() == 1);
		CHECK(entryBuffer.IsBlockDirty(0));
		CHECK_FALSE(handles[0].is_null());
		CHECK(handles[0].index == 0);
		CHECK(handles[0].generation == 0);
		CHECK(entryBuffer[handles[0]] == 1);

		entryBuffer.Update(cmdList);

		CHECK(entryBuffer.CountDirtyBlocks() == 0);
		CHECK_FALSE(entryBuffer.IsBlockDirty(0));

		entryBuffer.Set(handles[0], 2);
		CHECK(entryBuffer[handles[0]] == 2);

		REQUIRE(entryBuffer.CountDirtyBlocks() == 1);
		CHECK(entryBuffer.IsBlockDirty(0));

		entryBuffer.EmplaceBack(2);

		CHECK(entryBuffer.CountDirtyBlocks() == 2);
		CHECK(entryBuffer.IsBlockDirty(0));
		CHECK(entryBuffer.IsBlockDirty(1));

		entryBuffer.Update(cmdList);

		entryBuffer.Erase(handles[0]);
		handles[0] = entryBuffer.EmplaceBack(3);
		CHECK(handles[0].index == 0);
		CHECK(handles[0].generation == 1);
		CHECK(entryBuffer[handles[0]] == 3);

		handles[2] = entryBuffer.EmplaceBack(4);
		CHECK(handles[2].index == 2);
		CHECK(handles[2].generation == 0);
		CHECK(entryBuffer[handles[2]] == 4);
	}

	SECTION("Add and Set")
	{
		auto desc      = bgl::EntryBufferDesc();
		desc.maxCount  = 4;
		desc.blockSize = sizeof(int);
		desc.debugName = "EntryBuffer Add/Set";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		// Add() allocates a slot and stores the value in one step.
		auto slot = entryBuffer.Add(7);
		CHECK_FALSE(slot.is_null());
		CHECK(slot.index == 0);
		CHECK(slot.generation == 0);
		CHECK(entryBuffer[slot] == 7);
		CHECK(entryBuffer.CountDirtyBlocks() == 1);
		CHECK(entryBuffer.IsBlockDirty(0));

		entryBuffer.Update(cmdList);
		CHECK(entryBuffer.CountDirtyBlocks() == 0);

		// Set() overwrites the value and re-dirties its block.
		entryBuffer.Set(slot, 42);
		CHECK(entryBuffer[slot] == 42);
		CHECK(entryBuffer.CountDirtyBlocks() == 1);
		CHECK(entryBuffer.IsBlockDirty(0));
	}

	SECTION("Erase reuses the slot with a bumped generation")
	{
		auto desc      = bgl::EntryBufferDesc();
		desc.maxCount  = 4;
		desc.blockSize = sizeof(int);
		desc.debugName = "EntryBuffer Erase";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		auto a = entryBuffer.EmplaceBack(10);
		auto b = entryBuffer.EmplaceBack(20);
		auto c = entryBuffer.EmplaceBack(30);

		CHECK(a.index == 0);
		CHECK(b.index == 1);
		CHECK(c.index == 2);
		CHECK(entryBuffer[a] == 10);
		CHECK(entryBuffer[b] == 20);
		CHECK(entryBuffer[c] == 30);

		entryBuffer.Erase(b);

		// The next allocation reuses slot 1 with an incremented generation.
		auto reused = entryBuffer.EmplaceBack(99);
		CHECK(reused.index == 1);
		CHECK(reused.generation == 1);
		CHECK(entryBuffer[reused] == 99);
	}

	SECTION("Dirty block tracking across blocks")
	{
		auto desc      = bgl::EntryBufferDesc();
		desc.maxCount  = 8;
		desc.blockSize = 4 * sizeof(int);  // Four elements per block => 2 blocks.
		desc.debugName = "EntryBuffer Blocks";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		// First four entries land entirely in block 0.
		entryBuffer.EmplaceBack(0);
		entryBuffer.EmplaceBack(1);
		entryBuffer.EmplaceBack(2);
		entryBuffer.EmplaceBack(3);
		CHECK(entryBuffer.CountDirtyBlocks() == 1);
		CHECK(entryBuffer.IsBlockDirty(0));
		CHECK_FALSE(entryBuffer.IsBlockDirty(1));

		// The fifth entry crosses into block 1.
		entryBuffer.EmplaceBack(4);
		CHECK(entryBuffer.CountDirtyBlocks() == 2);
		CHECK(entryBuffer.IsBlockDirty(1));

		// Out-of-range block queries are false rather than a crash.
		CHECK_FALSE(entryBuffer.IsBlockDirty(9999));

		entryBuffer.Update(cmdList);
		CHECK(entryBuffer.CountDirtyBlocks() == 0);

		// Updating again with nothing dirty is a no-op.
		entryBuffer.Update(cmdList);
		CHECK(entryBuffer.CountDirtyBlocks() == 0);
	}

	cmdList->Close();
}
