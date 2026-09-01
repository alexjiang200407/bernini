// bgl_tests globs every .cpp under tests/ whatever the backend, so a Metal-only case has to exclude
// itself: a command buffer's retirement is only observable through metal-cpp.
#if defined(RENDERER_BACKEND_METAL)

#	include "cmd/CommandAllocator.h"
#	include "cmd/CommandList.h"
#	include "cmd/CommandQueue.h"
#	include "device/Device.h"
#	include "gfx/GraphicsBase.h"
#	include "metal/cmd/CommandList_metal.h"
#	include "resource/ResourceManager.h"
#	include "util/GpuValidation.h"
#	include "util/TestOptions.h"

#	include <bgl/IGraphics.h>

namespace
{
	// Each iteration is one flush, and an unretired buffer is the common case rather than a rare
	// one -- roughly seven in ten before the fix -- so this many makes a surviving bug certain to
	// show while keeping the case well under a second.
	constexpr int c_Flushes = 64;

	// Big enough that the driver has real work to retire; a trivially small copy retires so fast
	// that the window closes on its own.
	constexpr uint64_t c_CopyBytes = 4 * 1024 * 1024;
}

TEST_CASE("Flush leaves nothing for the driver to retire", "[teardown]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto  rm     = gfxBase->GetResourceManagerCpy();
	auto* device = gfxBase->GetDevice();

	auto queue = device->CreateCommandQueue(bgl::QueueType::kGraphics);
	rm->RegisterQueue(queue.Get());

	auto alloc    = device->CreateCommandAllocator();
	auto listDesc = bgl::CommandListDesc();
	listDesc.type = bgl::QueueType::kGraphics;
	auto list     = device->CreateCommandList(listDesc, alloc, rm);

	auto bufDesc      = bgl::RawViewDesc();
	bufDesc.byteSize  = c_CopyBytes;
	bufDesc.debugName = "flush idle probe";
	bufDesc.isUav     = true;

	const bgl::BufferHandle src = rm->CreateRawBuffer(bufDesc);
	const bgl::BufferHandle dst = rm->CreateRawBuffer(bufDesc);
	REQUIRE_FALSE(src.IsNull());
	REQUIRE_FALSE(dst.IsNull());

	// Flush is what every teardown, resize and WaitIdle calls to reach the state
	// `Destroy*(handle, false)` requires -- "the GPU is idle for that resource". Waiting on the
	// event the buffer signals does not reach it: the signal fires as the GPU passes it, while the
	// driver is still retiring the buffer and dropping what it held. So the buffer that was
	// executed before a flush must be Completed once that flush returns, or the caller frees
	// resources out from under the driver.
	int unretired = 0;
	for (int i = 0; i < c_Flushes; ++i)
	{
		alloc->ResetAllocator();
		list->Open(queue.Get(), alloc.Get());
		list->CopyBuffer(dst, src, 0, 0, c_CopyBytes);
		list->Close();

		MTL::CommandBuffer* cmdBuffer = list->As<bgl::CommandList>()->GetCommandBuffer();
		(void)queue->ExecuteCommandList(list.Get());

		queue->Flush();

		if (cmdBuffer->status() != MTL::CommandBufferStatusCompleted)
			++unretired;
	}

	CHECK(unretired == 0);

	rm->DestroyBuffer(dst, false);
	rm->DestroyBuffer(src, false);
	rm->UnregisterQueue(queue.Get());
}

#endif
