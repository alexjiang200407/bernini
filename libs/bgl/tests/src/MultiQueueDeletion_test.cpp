#include "gfx/GraphicsBase.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace
{
	bgl::test::GraphicsSetup
	HeadlessOptions()
	{
		auto opts                        = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer = false;
		return opts;
	}

	bgpu::TextureHandle
	MakeTexture(const bgpu::ResourceManagerRef& rm)
	{
		auto desc      = bgpu::TextureDesc();
		desc.format    = bgpu::Format::RGBA8_UNORM;
		desc.usage     = bgpu::TextureUsageFlag::kSRV;
		desc.debugName = "MultiQueueDeletion";
		return rm->CreateTexture(desc);
	}
}

// The point of the N-timeline gate: a resource retired while two queues are registered must survive
// until BOTH have passed the fence they were at, not just one. Advancing a single timeline is not
// enough to reclaim its descriptor slot.
TEST_CASE("A deferred free waits for every registered queue", "[resourcemanager][multiqueue]")
{
	auto gfx = bgl::test::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto  rm     = gfxBase->GetResourceManagerCpy();
	auto* device = gfxBase->GetDevice();

	// Two more timelines on top of the context's own, which Graphics registers at construction --
	// so the gate spans three, and WaitIdle() below is what clears the third.
	auto queueA = device->CreateCommandQueue(bgpu::QueueType::kGraphics);
	auto queueB = device->CreateCommandQueue(bgpu::QueueType::kGraphics);
	rm->RegisterQueue(queueA.Get());
	rm->RegisterQueue(queueB.Get());

	const bgpu::TextureHandle tex  = MakeTexture(rm);
	const uint32_t            slot = tex.slot.index;
	REQUIRE(rm->ValidTextureHandle(tex));

	// Retire it against both timelines. Neither has been advanced, so the gate holds each queue's
	// current fence.
	rm->DestroyTexture(tex);
	CHECK_FALSE(rm->ValidTextureHandle(tex));

	SECTION("advancing only one queue does not reclaim the slot")
	{
		// queueA and the context's queue reach their gates...
		queueA->Flush();
		gfxBase->WaitIdle();
		rm->CleanupExpiredResources();

		// ...but queueB has not, so the slot is still gated: a new texture must land elsewhere.
		const bgpu::TextureHandle other = MakeTexture(rm);
		CHECK(other.slot.index != slot);

		// Now queueB reaches its gate too. All cleared: the slot returns to the free list.
		queueB->Flush();
		rm->CleanupExpiredResources();

		const bgpu::TextureHandle recycled = MakeTexture(rm);
		CHECK(recycled.slot.index == slot);
	}

	SECTION("a queue that unregisters stops gating the free")
	{
		// queueB reaches its gate, then leaves. Unregistering scrubs it from the gate, so the
		// remaining timelines are queueA's and the context's.
		queueB->Flush();
		rm->UnregisterQueue(queueB.Get());

		queueA->Flush();
		gfxBase->WaitIdle();
		rm->CleanupExpiredResources();

		const bgpu::TextureHandle recycled = MakeTexture(rm);
		CHECK(recycled.slot.index == slot);
	}

	rm->UnregisterQueue(queueA.Get());
	rm->UnregisterQueue(queueB.Get());
}
