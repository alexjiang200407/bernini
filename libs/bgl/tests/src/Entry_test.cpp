#include "cmd/CommandAllocator.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "resource/Readback.h"
#include "scene/EntryBuffer.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

TEST_CASE("EntryBuffer", "[entry][scene]")
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

	SECTION("IsValid detects use-after-free")
	{
		auto desc      = bgl::EntryBufferDesc();
		desc.maxCount  = 4;
		desc.blockSize = sizeof(int);
		desc.debugName = "EntryBuffer IsValid";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		auto a = entryBuffer.EmplaceBack(10);
		CHECK(entryBuffer.IsValid(a));
		CHECK(entryBuffer.IsIndexValid(a.index));

		entryBuffer.Erase(a);
		CHECK_FALSE(entryBuffer.IsValid(a));
		CHECK_FALSE(entryBuffer.IsIndexValid(a.index));

		// Reusing the slot makes a fresh handle valid while the stale one (same
		// index, older generation) stays invalid.
		auto reused = entryBuffer.EmplaceBack(20);
		CHECK(reused.index == a.index);
		CHECK(entryBuffer.IsValid(reused));
		CHECK_FALSE(entryBuffer.IsValid(a));

		// A never-allocated handle is invalid rather than a crash.
		CHECK_FALSE(entryBuffer.IsValid(core::slot_handle{}));
	}

	SECTION("Metadata is per-slot and reset on reuse")
	{
		struct RefMeta
		{
			uint32_t refCount = 0;
		};

		auto desc      = bgl::EntryBufferDesc();
		desc.maxCount  = 4;
		desc.blockSize = sizeof(int);
		desc.debugName = "EntryBuffer Meta";

		auto entryBuffer = bgl::EntryBuffer<int, RefMeta>(desc, resourceManager);

		auto a = entryBuffer.Add(1);
		CHECK(entryBuffer.MetaAt(a.index).refCount == 0);

		entryBuffer.MetaAt(a.index).refCount = 3;
		CHECK(entryBuffer.MetaAt(a.index).refCount == 3);

		// A reused slot starts its metadata fresh.
		entryBuffer.Erase(a);
		auto reused = entryBuffer.Add(2);
		CHECK(reused.index == a.index);
		CHECK(entryBuffer.MetaAt(reused.index).refCount == 0);
	}

	// Regression: IssueCopy used to source every upload from the mirror's base, so a dirty run
	// past block 0 uploaded the mirror's FIRST bytes into a LATER GPU region.
	SECTION("A dirty slot past the first block uploads its own bytes")
	{
		auto desc      = bgl::EntryBufferDesc();
		desc.maxCount  = 16;
		desc.blockSize = 4 * sizeof(int);  // Four elements per block => 4 blocks.
		desc.debugName = "EntryBuffer Offset Upload";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		core::slot_handle handles[9];
		for (int i = 0; i < 9; ++i) handles[i] = entryBuffer.EmplaceBack(100 + i);
		entryBuffer.Update(cmdList);

		// Only block 2 goes dirty: its upload sources the mirror at that offset, not the
		// mirror's start.
		entryBuffer.Set(handles[8], 999);
		entryBuffer.Update(cmdList);

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = desc.maxCount * sizeof(int);
		rbDesc.debugName = "EntryBuffer Offset Upload Readback";
		auto readback    = resourceManager->CreateReadbackBuffer(rbDesc);

		auto barrier = bgl::BufferBarrierDesc();
		barrier.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource);
		cmdList->Barrier(entryBuffer.GetBufferHandle(), barrier);

		cmdList->CopyBufferToReadback(readback, entryBuffer.GetBufferHandle());
		cmdList->Close();

		auto fence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(fence);

		const auto* mapped = static_cast<const int*>(resourceManager->MapReadback(readback));
		REQUIRE(mapped != nullptr);

		for (int i = 0; i < 8; ++i)
		{
			CHECK(mapped[i] == 100 + i);
		}
		CHECK(mapped[8] == 999);

		resourceManager->UnmapReadback(readback);
		resourceManager->DestroyReadbackBuffer(readback, fence, false);
		entryBuffer.Release(fence, false);

		// The case-wide Close below expects an open list.
		cmdList->Open(cmdQueue, cmdAllocator);
	}

	cmdList->Close();
}
