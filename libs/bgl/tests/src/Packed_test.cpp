#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "scene/PackedBuffer.h"
#include <bgl/IGraphics.h>

TEST_CASE("PackedBuffer", "[packed][scene]")
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

	using PackedInt = bgl::PackedBuffer<int>;

	SECTION("EmplaceBack and access by handle")
	{
		auto desc      = bgl::PackedBufferDesc();
		desc.maxCount  = 4;
		desc.blockSize = sizeof(int);
		desc.debugName = "PackedBuffer Emplace";

		auto pb = PackedInt(desc, resourceManager);

		CHECK(pb.IsEmpty());
		CHECK(pb.Count() == 0);

		auto h0 = pb.EmplaceBack(10);
		auto h1 = pb.EmplaceBack(20);
		auto h2 = pb.Add(30);

		CHECK_FALSE(pb.IsEmpty());
		CHECK(pb.Count() == 3);

		CHECK(pb.IsValid(h0));
		CHECK(pb.IsValid(h1));
		CHECK(pb.IsValid(h2));

		CHECK(pb[h0] == 10);
		CHECK(pb[h1] == 20);
		CHECK(pb[h2] == 30);

		// The three elements occupy dense slots 0, 1, 2, each marking its block.
		CHECK(pb.CountDirtyBlocks() == 3);
		CHECK(pb.IsBlockDirty(0));
		CHECK(pb.IsBlockDirty(1));
		CHECK(pb.IsBlockDirty(2));
		CHECK_FALSE(pb.IsBlockDirty(3));
	}

	SECTION("Set updates a value via its handle")
	{
		auto desc      = bgl::PackedBufferDesc();
		desc.maxCount  = 4;
		desc.blockSize = sizeof(int);
		desc.debugName = "PackedBuffer Set";

		auto pb = PackedInt(desc, resourceManager);

		auto h0 = pb.EmplaceBack(1);
		pb.Update(cmdList);
		CHECK(pb.CountDirtyBlocks() == 0);

		pb.Set(h0, 42);
		CHECK(pb[h0] == 42);
		CHECK(pb.CountDirtyBlocks() == 1);
		CHECK(pb.IsBlockDirty(0));
	}

	SECTION("Erase keeps other handles valid despite the swap")
	{
		auto desc      = bgl::PackedBufferDesc();
		desc.maxCount  = 4;
		desc.blockSize = sizeof(int);
		desc.debugName = "PackedBuffer Erase";

		auto pb = PackedInt(desc, resourceManager);

		auto h0 = pb.EmplaceBack(10);
		auto h1 = pb.EmplaceBack(20);
		auto h2 = pb.EmplaceBack(30);
		auto h3 = pb.EmplaceBack(40);
		REQUIRE(pb.Count() == 4);

		// Erasing a middle element swaps the last (40) into its slot, but the
		// handle indirection hides that entirely from the caller.
		pb.Erase(h1);
		CHECK(pb.Count() == 3);

		CHECK_FALSE(pb.IsValid(h1));
		CHECK(pb.IsValid(h0));
		CHECK(pb.IsValid(h2));
		CHECK(pb.IsValid(h3));

		// Every surviving handle still resolves to its original value, including
		// h3 whose element physically relocated.
		CHECK(pb[h0] == 10);
		CHECK(pb[h2] == 30);
		CHECK(pb[h3] == 40);
	}

	SECTION("Erasing the last element moves nothing")
	{
		auto desc      = bgl::PackedBufferDesc();
		desc.maxCount  = 4;
		desc.blockSize = sizeof(int);
		desc.debugName = "PackedBuffer EraseLast";

		auto pb = PackedInt(desc, resourceManager);

		auto h0 = pb.EmplaceBack(10);
		auto h1 = pb.EmplaceBack(20);  // dense tail
		REQUIRE(pb.Count() == 2);

		pb.Update(cmdList);
		CHECK(pb.CountDirtyBlocks() == 0);

		pb.Erase(h1);
		CHECK(pb.Count() == 1);
		CHECK_FALSE(pb.IsValid(h1));
		CHECK(pb[h0] == 10);

		// Nothing relocated into a live slot, so nothing needs re-uploading.
		CHECK(pb.CountDirtyBlocks() == 0);
	}

	SECTION("Erase re-dirties the swapped-in slot")
	{
		auto desc      = bgl::PackedBufferDesc();
		desc.maxCount  = 4;
		desc.blockSize = sizeof(int);
		desc.debugName = "PackedBuffer EraseDirty";

		auto pb = PackedInt(desc, resourceManager);

		auto h0 = pb.EmplaceBack(10);  // dense 0
		auto h1 = pb.EmplaceBack(20);  // dense 1
		auto h2 = pb.EmplaceBack(30);  // dense 2

		pb.Update(cmdList);
		CHECK(pb.CountDirtyBlocks() == 0);

		// h2's element (30) is swapped into dense slot 0, so block 0 is re-dirtied.
		pb.Erase(h0);
		CHECK_FALSE(pb.IsValid(h0));
		CHECK(pb[h2] == 30);
		CHECK(pb[h1] == 20);
		CHECK(pb.CountDirtyBlocks() == 1);
		CHECK(pb.IsBlockDirty(0));
	}

	SECTION("Erase down to empty then refill")
	{
		auto desc      = bgl::PackedBufferDesc();
		desc.maxCount  = 4;
		desc.blockSize = sizeof(int);
		desc.debugName = "PackedBuffer Refill";

		auto pb = PackedInt(desc, resourceManager);

		auto h0 = pb.EmplaceBack(1);
		auto h1 = pb.EmplaceBack(2);

		pb.Erase(h0);
		pb.Erase(h1);
		CHECK(pb.IsEmpty());
		CHECK(pb.Count() == 0);

		auto h2 = pb.EmplaceBack(99);
		CHECK(pb.Count() == 1);
		CHECK(pb.IsValid(h2));
		CHECK(pb[h2] == 99);

		// The handles to the erased elements stay invalid even after reuse.
		CHECK_FALSE(pb.IsValid(h0));
		CHECK_FALSE(pb.IsValid(h1));
	}

	SECTION("Update clears dirty blocks and is a no-op when clean")
	{
		auto desc      = bgl::PackedBufferDesc();
		desc.maxCount  = 8;
		desc.blockSize = sizeof(int);
		desc.debugName = "PackedBuffer Update";

		auto pb = PackedInt(desc, resourceManager);

		pb.EmplaceBack(1);
		pb.EmplaceBack(2);
		CHECK(pb.CountDirtyBlocks() == 2);

		pb.Update(cmdList);
		CHECK(pb.CountDirtyBlocks() == 0);

		// Updating again with nothing dirty does nothing.
		pb.Update(cmdList);
		CHECK(pb.CountDirtyBlocks() == 0);
	}

	SECTION("Dirty tracking spans multiple blocks")
	{
		auto desc      = bgl::PackedBufferDesc();
		desc.maxCount  = 8;
		desc.blockSize = 4 * sizeof(int);  // Four elements per block => 2 blocks.
		desc.debugName = "PackedBuffer Blocks";

		auto pb = PackedInt(desc, resourceManager);

		// First four entries land in block 0.
		pb.EmplaceBack(0);
		pb.EmplaceBack(1);
		pb.EmplaceBack(2);
		pb.EmplaceBack(3);
		CHECK(pb.CountDirtyBlocks() == 1);
		CHECK(pb.IsBlockDirty(0));
		CHECK_FALSE(pb.IsBlockDirty(1));

		// The fifth entry crosses into block 1.
		pb.EmplaceBack(4);
		CHECK(pb.CountDirtyBlocks() == 2);
		CHECK(pb.IsBlockDirty(1));

		// Out-of-range block queries are false rather than a crash.
		CHECK_FALSE(pb.IsBlockDirty(9999));
	}

	cmdList->Close();
}
