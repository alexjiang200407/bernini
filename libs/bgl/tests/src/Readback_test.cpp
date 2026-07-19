#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include "types/ClearValue.h"
#include "types/Format.h"
#include "util/GpuValidation.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>

namespace
{
	// Shared setup: a headless graphics device and one command list/queue. Each readback test gets a
	// fresh instance (Catch constructs the fixture per test case).
	struct ReadbackFixture
	{
		bgl::GraphicsRef         gfx;
		bgl::GraphicsBase*       gfxBase = nullptr;
		bgl::ResourceManagerRef  resourceManager;
		bgl::IDevice*            device = nullptr;
		bgl::CommandAllocatorRef cmdAllocator;
		bgl::CommandListRef      cmdList;
		bgl::CommandQueueRef     cmdQueue;

		ReadbackFixture()
		{
			auto opts                     = bgl::GraphicsOptions();
			opts.enableDebugLayer         = true;
			opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
			opts.enablePixDebug           = true;

			gfx = bgl::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			gfxBase = gfx->As<bgl::GraphicsBase>();
			REQUIRE(gfxBase != nullptr);

			resourceManager = gfxBase->GetResourceManagerCpy();
			REQUIRE(resourceManager != nullptr);

			device = gfxBase->GetDevice();

			auto cmdListDesc = bgl::CommandListDesc();
			cmdListDesc.type = bgl::QueueType::kGraphics;

			cmdAllocator = device->CreateCommandAllocator();
			cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
			cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);
		}
	};
}

TEST_CASE_METHOD(ReadbackFixture, "Buffer readback", "[readback][metal]")
{
	const uint32_t values[] = { 11, 22, 33, 44, 55, 66, 77, 88 };

	auto bufDesc         = bgl::StructBufferDesc();
	bufDesc.stride       = sizeof(uint32_t);
	bufDesc.elementCount = static_cast<uint32_t>(std::size(values));
	bufDesc.debugName    = "Readback Source Buffer";

	auto src = resourceManager->CreateStructBuffer(bufDesc);

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = sizeof(values);
	rbDesc.debugName = "Readback Buffer";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	cmdList->WriteBuffer(src, values, sizeof(values));

	// The write lands via a copy, so move the buffer from copy-dest to
	// copy-source before reading it back.
	auto barrier = bgl::BufferBarrierDesc();
	barrier.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
		.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
		.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
		.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource);
	cmdList->Barrier(src, barrier);

	cmdList->CopyBufferToReadback(rb, src);
	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(rb));
	REQUIRE(mapped != nullptr);

	for (uint32_t i = 0; i < std::size(values); ++i)
	{
		CHECK(mapped[i] == values[i]);
	}

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, fence, false);
	resourceManager->DestroyBuffer(src, fence, false);
}

TEST_CASE_METHOD(ReadbackFixture, "Texture readback", "[readback][metal]")
{
	const uint32_t width    = 4;
	const uint32_t height   = 4;
	const float    clear[4] = { 0.25f, 0.5f, 0.75f, 1.0f };

	// RGBA32_FLOAT so the cleared values come back bit-for-bit.
	auto texDesc          = bgl::TextureDesc();
	texDesc.width         = width;
	texDesc.height        = height;
	texDesc.format        = bgl::Format::RGBA32_FLOAT;
	texDesc.usage         = bgl::TextureUsageFlag::kRenderTarget;
	texDesc.initialLayout = bgl::BarrierLayout::kRenderTarget;
	texDesc.debugName     = "Readback Source Texture";
	texDesc.clearValue.SetColor(bgl::Color(clear[0], clear[1], clear[2], clear[3]));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc      = bgl::RtvDesc();
	rtvDesc.format    = bgl::Format::RGBA32_FLOAT;
	rtvDesc.debugName = "Readback Source RTV";

	auto rtv = resourceManager->CreateRtv(tex, rtvDesc);

	auto layout = resourceManager->GetTextureReadbackLayout(tex);
	REQUIRE(layout.rowCount == height);
	REQUIRE(layout.rowSizeBytes == width * 4 * sizeof(float));

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Readback Buffer (Texture)";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	float clearColor[4] = { clear[0], clear[1], clear[2], clear[3] };
	resourceManager->ClearRtv(cmdList, rtv, clearColor);

	// Move the texture from render-target to copy-source for the readback.
	auto barrier = bgl::TextureBarrierDesc();
	barrier.AddSyncBefore(bgl::BarrierSyncFlag::kRenderTarget)
		.AddAccessBefore(bgl::BarrierAccessFlag::kRenderTarget)
		.SetLayoutBefore(bgl::BarrierLayout::kRenderTarget)
		.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
		.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource)
		.SetLayoutAfter(bgl::BarrierLayout::kCopySource);
	cmdList->Barrier(tex, barrier);

	cmdList->CopyTextureToReadback(rb, tex);
	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* base = static_cast<const uint8_t*>(resourceManager->MapReadback(rb));
	REQUIRE(base != nullptr);

	// Every texel should equal the clear color. Rows are padded to
	// layout.rowPitch, so index each row by that pitch.
	for (uint32_t y = 0; y < height; ++y)
	{
		const auto* row =
			reinterpret_cast<const float*>(base + layout.offset + y * layout.rowPitch);

		for (uint32_t x = 0; x < width; ++x)
		{
			CHECK(row[x * 4 + 0] == Catch::Approx(clear[0]));
			CHECK(row[x * 4 + 1] == Catch::Approx(clear[1]));
			CHECK(row[x * 4 + 2] == Catch::Approx(clear[2]));
			CHECK(row[x * 4 + 3] == Catch::Approx(clear[3]));
		}
	}

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, fence, false);
	resourceManager->DestroyRtv(rtv, fence, false);
	resourceManager->DestroyTexture(tex, fence, false);
}
