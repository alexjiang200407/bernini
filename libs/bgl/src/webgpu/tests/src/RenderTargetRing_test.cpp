#include "RenderTarget_wgpu.h"
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "resource/ResourceManager.h"

#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The headless RenderTarget on WebGPU: the frame ring RenderTargetBase promises, over real
// backbuffers. Everything here is what GraphicsBase's frame loop will drive once CreateGraphics
// works on this backend, so the contract is pinned now, test-first, while nothing calls it.

namespace
{
	struct Fixture
	{
		core::SharedRef<Device> device  = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
		ResourceManagerRef      manager = device->CreateResourceManager(ResourceManagerDesc{});
		CommandQueueRef         queue   = device->CreateCommandQueue(QueueType::kGraphics);

		Fixture() { manager->RegisterQueue(queue.Get()); }

		~Fixture()
		{
			queue->Flush();
			manager->UnregisterQueue(queue.Get());
		}

		core::SharedRef<RenderTarget>
		MakeTarget(int width, int height)
		{
			auto desc     = RenderTargetDesc{};
			desc.width    = width;
			desc.height   = height;
			desc.headless = true;

			return core::SharedRef<RenderTarget>::Make(desc, device, queue, manager);
		}
	};
}

TEST_CASE("A headless render target owns a full frame ring", "[wgpu][rendertarget]")
{
	auto fixture = Fixture{};
	auto target  = fixture.MakeTarget(64, 32);

	CHECK(target->GetWidth() == 64);
	CHECK(target->GetHeight() == 32);
	CHECK(target->IsHeadless());

	for (uint32_t i = 0; i < c_SwapchainImageCount; ++i)
	{
		REQUIRE(fixture.manager->ValidTextureHandle(target->BackbufferTexture(i)));
		REQUIRE(fixture.manager->ValidRtvHandle(target->BackbufferRtv(i)));
		REQUIRE(target->FrameAllocator(i) != nullptr);
		CHECK(target->FrameFence(i) == 0);
	}

	// Distinct backbuffers and allocators per frame in flight, or the ring is a ring of one.
	CHECK(target->BackbufferTexture(0).slot.index != target->BackbufferTexture(1).slot.index);
	CHECK(target->FrameAllocator(0) != target->FrameAllocator(1));

	REQUIRE(fixture.manager->ValidDsvHandle(target->DepthDsv()));
	REQUIRE(fixture.manager->ValidTextureHandle(target->GetMotionVectorTexture()));
	REQUIRE(fixture.manager->ValidRtvHandle(target->GetMotionVectorRtv()));

	const TextureDesc backbuffer = fixture.manager->GetTextureDesc(target->BackbufferTexture(0));
	CHECK(backbuffer.width == 64);
	CHECK(backbuffer.height == 32);
	CHECK(backbuffer.format == Format::SBGRA8_UNORM);
}

TEST_CASE("PresentAndAdvance walks the ring and trails the presented index", "[wgpu][rendertarget]")
{
	auto fixture = Fixture{};
	auto target  = fixture.MakeTarget(16, 16);

	CHECK(target->FrameIndex() == 0);

	target->SetFrameFence(0, 7);
	CHECK(target->FrameFence(0) == 7);

	target->PresentAndAdvance();
	CHECK(target->LastPresentedIndex() == 0);
	CHECK(target->FrameIndex() == 1);

	target->PresentAndAdvance();
	CHECK(target->LastPresentedIndex() == 1);
	CHECK(target->FrameIndex() == 0);

	// The fence survives the wrap: it describes frame 0's last submission until overwritten.
	CHECK(target->FrameFence(0) == 7);
}

TEST_CASE("Resizing recreates the backbuffers and resets the ring", "[wgpu][rendertarget]")
{
	auto fixture = Fixture{};
	auto target  = fixture.MakeTarget(32, 32);

	const auto oldBackbuffer = target->BackbufferTexture(0);
	const auto oldDepth      = target->DepthDsv();

	target->SetFrameFence(0, 3);
	target->SetFrameFence(1, 4);
	target->PresentAndAdvance();

	target->ResizeBackbuffers(128, 64);

	CHECK(target->GetWidth() == 128);
	CHECK(target->GetHeight() == 64);
	CHECK(target->FrameIndex() == 0);
	CHECK(target->LastPresentedIndex() == 0);

	// The old handles staled with their textures; the new ring is live at the new size.
	CHECK_FALSE(fixture.manager->ValidTextureHandle(oldBackbuffer));
	CHECK_FALSE(fixture.manager->ValidDsvHandle(oldDepth));

	for (uint32_t i = 0; i < c_SwapchainImageCount; ++i)
	{
		REQUIRE(fixture.manager->ValidTextureHandle(target->BackbufferTexture(i)));
		CHECK(target->FrameFence(i) == 0);
	}

	CHECK(fixture.manager->GetTextureDesc(target->BackbufferTexture(0)).width == 128);
}

TEST_CASE("A frame renders into the ring's own backbuffer", "[wgpu][rendertarget]")
{
	constexpr uint32_t c_Size = 16;

	auto fixture = Fixture{};
	auto target  = fixture.MakeTarget(c_Size, c_Size);

	const uint32_t frame = target->FrameIndex();

	auto list = fixture.device->CreateCommandList(
		CommandListDesc{},
		core::SharedRef<ICommandAllocator>(target->FrameAllocator(frame)),
		fixture.manager);

	const auto backbuffer = target->BackbufferTexture(frame);
	const auto rbLayout   = fixture.manager->GetTextureReadbackLayout(backbuffer);
	const auto readback   = fixture.manager->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = rbLayout.totalBytes, .debugName = "bb-readback" });

	float clear[4] = { 0.0f, 1.0f, 0.0f, 1.0f };

	list->Open(fixture.queue.Get(), target->FrameAllocator(frame));
	fixture.manager->ClearRtv(list.Get(), target->BackbufferRtv(frame), clear);
	list->CopyTextureToReadback(readback, backbuffer);
	list->Close();

	const auto fence = fixture.queue->ExecuteCommandList(list.Get());
	target->SetFrameFence(frame, fence);
	fixture.queue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(fixture.manager->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	// Rows are pitched to 256 bytes in the readback, so address by row, not flat index.
	const auto at = [&](uint32_t x, uint32_t y) {
		return mapped[y * (rbLayout.rowPitch / sizeof(uint32_t)) + x];
	};

	// BGRA in memory: green clear -> 0xFF00FF00.
	CHECK(at(0, 0) == 0xFF00FF00u);
	CHECK(at(c_Size - 1, c_Size - 1) == 0xFF00FF00u);

	fixture.manager->UnmapReadback(readback);

	target->PresentAndAdvance();
	CHECK(target->LastPresentedIndex() == frame);
}
