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
		auto desc         = bgl::EntryBufferDesc();
		desc.initialCount = 3;
		desc.blockSize    = sizeof(int);
		desc.debugName    = "Test Entry Buffer";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		// Construction reserves the null element and leaves its block dirty, so the GPU is given a
		// zeroed element 0 on the first flush.
		REQUIRE(entryBuffer.CountDirtyBlocks() == 1);
		CHECK(entryBuffer.IsBlockDirty(0));
		entryBuffer.Update(cmdList);

		core::slot_handle handles[3];

		handles[0] = entryBuffer.EmplaceBack(1);
		REQUIRE(entryBuffer.CountDirtyBlocks() == 1);
		CHECK(entryBuffer.IsBlockDirty(1));
		CHECK_FALSE(handles[0].is_null());
		CHECK(handles[0].index == 1);
		CHECK(handles[0].generation == 0);
		CHECK(entryBuffer[handles[0]] == 1);

		entryBuffer.Update(cmdList);

		CHECK(entryBuffer.CountDirtyBlocks() == 0);
		CHECK_FALSE(entryBuffer.IsBlockDirty(1));

		entryBuffer.Set(handles[0], 2);
		CHECK(entryBuffer[handles[0]] == 2);

		REQUIRE(entryBuffer.CountDirtyBlocks() == 1);
		CHECK(entryBuffer.IsBlockDirty(1));

		entryBuffer.EmplaceBack(2);

		CHECK(entryBuffer.CountDirtyBlocks() == 2);
		CHECK(entryBuffer.IsBlockDirty(1));
		CHECK(entryBuffer.IsBlockDirty(2));

		entryBuffer.Update(cmdList);

		entryBuffer.Erase(handles[0]);
		handles[0] = entryBuffer.EmplaceBack(3);
		CHECK(handles[0].index == 1);
		CHECK(handles[0].generation == 1);
		CHECK(entryBuffer[handles[0]] == 3);

		handles[2] = entryBuffer.EmplaceBack(4);
		CHECK(handles[2].index == 3);
		CHECK(handles[2].generation == 0);
		CHECK(entryBuffer[handles[2]] == 4);
	}

	SECTION("The null offset is never handed out")
	{
		auto desc         = bgl::EntryBufferDesc();
		desc.initialCount = 4;
		desc.blockSize    = sizeof(int);
		desc.debugName    = "EntryBuffer Null Offset";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		// The reserved element is not a live entry, so an offset read back from a GPU-side struct
		// that never had a handle assigned resolves to nothing.
		CHECK_FALSE(entryBuffer.IsIndexValid(0));

		// initialCount is the caller's budget: the reserved element rides on top of it, so four
		// allocations still fit without growing.
		for (int i = 0; i < 4; ++i)
		{
			const core::slot_handle slot = entryBuffer.EmplaceBack(i);
			CHECK(slot.index != 0);
			CHECK(entryBuffer.IsIndexValid(slot.index));
		}

		CHECK(entryBuffer.Capacity() == desc.initialCount + 1);
	}

	SECTION("Add and Set")
	{
		auto desc         = bgl::EntryBufferDesc();
		desc.initialCount = 4;
		desc.blockSize    = sizeof(int);
		desc.debugName    = "EntryBuffer Add/Set";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		// Flushes the reserved null element, so the counts below are the caller's writes alone.
		entryBuffer.Update(cmdList);

		// Add() allocates a slot and stores the value in one step.
		auto slot = entryBuffer.Add(7);
		CHECK_FALSE(slot.is_null());
		CHECK(slot.index == 1);
		CHECK(slot.generation == 0);
		CHECK(entryBuffer[slot] == 7);
		CHECK(entryBuffer.CountDirtyBlocks() == 1);
		CHECK(entryBuffer.IsBlockDirty(1));

		entryBuffer.Update(cmdList);
		CHECK(entryBuffer.CountDirtyBlocks() == 0);

		// Set() overwrites the value and re-dirties its block.
		entryBuffer.Set(slot, 42);
		CHECK(entryBuffer[slot] == 42);
		CHECK(entryBuffer.CountDirtyBlocks() == 1);
		CHECK(entryBuffer.IsBlockDirty(1));
	}

	SECTION("Erase reuses the slot with a bumped generation")
	{
		auto desc         = bgl::EntryBufferDesc();
		desc.initialCount = 4;
		desc.blockSize    = sizeof(int);
		desc.debugName    = "EntryBuffer Erase";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		auto a = entryBuffer.EmplaceBack(10);
		auto b = entryBuffer.EmplaceBack(20);
		auto c = entryBuffer.EmplaceBack(30);

		CHECK(a.index == 1);
		CHECK(b.index == 2);
		CHECK(c.index == 3);
		CHECK(entryBuffer[a] == 10);
		CHECK(entryBuffer[b] == 20);
		CHECK(entryBuffer[c] == 30);

		entryBuffer.Erase(b);

		// The next allocation reuses slot 2 with an incremented generation.
		auto reused = entryBuffer.EmplaceBack(99);
		CHECK(reused.index == 2);
		CHECK(reused.generation == 1);
		CHECK(entryBuffer[reused] == 99);
	}

	SECTION("Dirty block tracking across blocks")
	{
		auto desc         = bgl::EntryBufferDesc();
		desc.initialCount = 8;
		desc.blockSize    = 4 * sizeof(int);  // Four elements per block => 2 blocks.
		desc.debugName    = "EntryBuffer Blocks";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		// The reserved null element already occupies the first of block 0's four, so three entries
		// fill it.
		entryBuffer.EmplaceBack(0);
		entryBuffer.EmplaceBack(1);
		entryBuffer.EmplaceBack(2);
		CHECK(entryBuffer.CountDirtyBlocks() == 1);
		CHECK(entryBuffer.IsBlockDirty(0));
		CHECK_FALSE(entryBuffer.IsBlockDirty(1));

		// The fourth entry crosses into block 1.
		entryBuffer.EmplaceBack(3);
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
		auto desc         = bgl::EntryBufferDesc();
		desc.initialCount = 4;
		desc.blockSize    = sizeof(int);
		desc.debugName    = "EntryBuffer IsValid";

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

		auto desc         = bgl::EntryBufferDesc();
		desc.initialCount = 4;
		desc.blockSize    = sizeof(int);
		desc.debugName    = "EntryBuffer Meta";

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
		auto desc         = bgl::EntryBufferDesc();
		desc.initialCount = 16;
		desc.blockSize    = 4 * sizeof(int);  // Four elements per block => 4 blocks.
		desc.debugName    = "EntryBuffer Offset Upload";

		auto entryBuffer = bgl::EntryBuffer<int>(desc, resourceManager);

		core::slot_handle handles[9];
		for (int i = 0; i < 9; ++i) handles[i] = entryBuffer.EmplaceBack(100 + i);
		entryBuffer.Update(cmdList);

		// Only block 2 goes dirty: its upload sources the mirror at that offset, not the
		// mirror's start.
		entryBuffer.Set(handles[8], 999);
		entryBuffer.Update(cmdList);

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = entryBuffer.Capacity() * sizeof(int);
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

		// The reserved element uploads as zero, so a shader dereferencing a null entry reads a
		// zeroed element rather than whatever the allocation happened to contain.
		CHECK(mapped[0] == 0);

		for (int i = 0; i < 8; ++i)
		{
			CHECK(mapped[i + 1] == 100 + i);
		}
		CHECK(mapped[9] == 999);

		resourceManager->UnmapReadback(readback);
		resourceManager->DestroyReadbackBuffer(readback, false);
		entryBuffer.Release(false);

		// The case-wide Close below expects an open list.
		cmdList->Open(cmdQueue, cmdAllocator);
	}

	cmdList->Close();
}
