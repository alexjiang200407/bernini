// bgl_extended_tests globs every .cpp under tests/ whatever the backend, so a Metal-only case has to exclude
// itself: autorelease pools are the Metal backend's problem alone.
#include "types/QueueType.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#if defined(RENDERER_BACKEND_METAL)

#	include "cmd/CommandQueue.h"
#	include "device/Device.h"
#	include "gfx/GraphicsBase.h"
#	include "metal/cmd/CommandQueue_metal.h"
#	include "util/GpuValidation.h"
#	include "util/TestOptions.h"

#	include <bgl/IGraphics.h>

namespace
{
	constexpr uint32_t c_Flushes = 128;

	// The driver keeps its own reference to a buffer it has not yet retired, so a handful of retains
	// may still be outstanding when the loop ends. What must never happen is one per flush.
	constexpr NS::UInteger c_MaxRetained = c_Flushes / 8;
}

TEST_CASE("Flush leaves no command buffer in the enclosing pool", "[teardown]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	bgl::CommandQueueRef queue =
		gfxBase->GetDevice()->CreateCommandQueue(bgl::QueueType::kGraphics);
	REQUIRE(queue != nullptr);

	MTL::CommandQueue* mtlQueue = queue->As<bgl::CommandQueue>()->GetMTLCommandQueue();
	REQUIRE(mtlQueue != nullptr);

	// A command buffer holds its queue, which holds the device -- the chain the teardown crash walked
	// when a buffer outlived both. Left in a pool, each flush's buffer shows up here as a retain that
	// never comes back, so a count that scales with the flushes is the leak itself.
	NS::SharedPtr<NS::AutoreleasePool> pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

	const NS::UInteger before = mtlQueue->retainCount();

	for (uint32_t i = 0; i < c_Flushes; ++i)
	{
		queue->Flush();
	}

	CHECK(mtlQueue->retainCount() < before + c_MaxRetained);
}

#endif
