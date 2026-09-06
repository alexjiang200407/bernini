#include "gfx/GraphicsBase.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// Once descriptors come from an allocator, freeing one returns it to a free list the next create
// draws from -- so a recycled index is where an off-by-one or a double-free hands out a descriptor
// that is still live. That renders something plausible rather than crashing, which is why this
// churns rather than creating once.
TEST_CASE("A freed descriptor is reused without aliasing a live one", "[resource][bindless]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto resourceManager = gfx->As<bgl::GraphicsBase>()->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	const auto makeBuffer = [&](const char* name) {
		auto desc = bgl::ComputeBufferDesc();
		desc.SetElement<uint32_t>().SetInitialCount(4).SetDebugName(name);
		return resourceManager->CreateComputeBuffer(desc);
	};

	const bgl::BufferHandle first  = makeBuffer("Churn First");
	const bgl::BufferHandle second = makeBuffer("Churn Second");
	REQUIRE(resourceManager->ValidBufferHandle(first));
	REQUIRE(resourceManager->ValidBufferHandle(second));

	CHECK(first.bindlessIndex != second.bindlessIndex);

	// Immediate, not deferred: nothing has been submitted, so the descriptor is free to reuse now.
	resourceManager->DestroyBuffer(first, /*deferred*/ false);

	const bgl::BufferHandle third = makeBuffer("Churn Third");
	REQUIRE(resourceManager->ValidBufferHandle(third));

	// The survivor keeps its descriptor and stays live; the newcomer must not have been handed it.
	CHECK(third.bindlessIndex != second.bindlessIndex);
	CHECK(resourceManager->ValidBufferHandle(second));

	// The retired handle is stale even though its descriptor index has been handed out again.
	CHECK_FALSE(resourceManager->ValidBufferHandle(first));

	resourceManager->DestroyBuffer(second, /*deferred*/ false);
	resourceManager->DestroyBuffer(third, /*deferred*/ false);
}
