#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "resource/ResourceManager.h"
#include "resource/Texture.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

namespace
{
	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
		return opts;
	}

	bgl::TextureHandle
	MakeTexture(const bgl::ResourceManagerRef& rm)
	{
		auto desc      = bgl::TextureDesc();
		desc.format    = bgl::Format::RGBA8_UNORM;
		desc.usage     = bgl::TextureUsageFlag::kSRV;
		desc.debugName = "MultiQueueDeletion";
		return rm->CreateTexture(desc);
	}
}

// The point of the N-timeline gate: a resource retired while two queues are registered must survive
// until BOTH have passed the fence they were at, not just one. Advancing a single timeline is not
// enough to reclaim its descriptor slot.
TEST_CASE("A deferred free waits for every registered queue", "[resourcemanager][multiqueue]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto  rm     = gfxBase->GetResourceManagerCpy();
	auto* device = gfxBase->GetDevice();

	// The primary context's queue is already registered; add a second, independent timeline.
	auto queueB = device->CreateCommandQueue(bgl::QueueType::kGraphics);
	rm->RegisterQueue(queueB.Get());

	const bgl::TextureHandle tex  = MakeTexture(rm);
	const uint32_t           slot = tex.slot.index;
	REQUIRE(rm->ValidTextureHandle(tex));

	// Retire it against both timelines. Neither has been advanced, so the gate holds each queue's
	// current fence.
	rm->DestroyTexture(tex);
	CHECK_FALSE(rm->ValidTextureHandle(tex));

	SECTION("advancing only one queue does not reclaim the slot")
	{
		// The primary queue reaches its gate...
		gfxBase->WaitIdle();
		rm->CleanupExpiredResources();

		// ...but queueB has not, so the slot is still gated: a new texture must land elsewhere.
		const bgl::TextureHandle other = MakeTexture(rm);
		CHECK(other.slot.index != slot);

		// Now queueB reaches its gate too. Both cleared: the slot returns to the free list.
		queueB->Flush();
		rm->CleanupExpiredResources();

		const bgl::TextureHandle recycled = MakeTexture(rm);
		CHECK(recycled.slot.index == slot);
	}

	SECTION("a queue that unregisters stops gating the free")
	{
		// queueB never advances, but its context flushes and leaves. Unregistering scrubs it from the
		// gate, so the only remaining timeline is the primary queue.
		queueB->Flush();
		rm->UnregisterQueue(queueB.Get());

		gfxBase->WaitIdle();
		rm->CleanupExpiredResources();

		const bgl::TextureHandle recycled = MakeTexture(rm);
		CHECK(recycled.slot.index == slot);
	}

	rm->UnregisterQueue(queueB.Get());
}
