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

	cmdList->Close();
}
