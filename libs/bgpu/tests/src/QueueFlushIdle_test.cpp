// bgpu_tests globs every .cpp under tests/ whatever the backend, so a Metal-only case has to exclude
// itself: a command buffer's retirement is only observable through metal-cpp.
#if defined(RENDERER_BACKEND_METAL)

#	include "cmd/CommandList_metal.h"
#	include <bgpu/GpuContext.h>
#	include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#	include <bgpu/cmd/CommandList.h>
#	include <bgpu/cmd/CommandQueue.h>  // IWYU pragma: keep
#	include <bgpu/device/Device.h>
#	include <bgpu/resource/Buffer.h>
#	include <bgpu/resource/ResourceManager.h>
#	include <bgpu/types/QueueType.h>
#	include <catch2/catch_test_macros.hpp>
#	include <cstdint>

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
	auto contextDesc             = bgpu::GpuContextDesc();
	contextDesc.enableDebugLayer = true;
	auto context                 = bgpu::CreateGpuContext(contextDesc);

	auto device = bgpu::CreateDevice(context);
	auto rm     = device->CreateResourceManager(bgpu::ResourceManagerDesc());

	auto queue = device->CreateCommandQueue(bgpu::QueueType::kGraphics);
	rm->RegisterQueue(queue.Get());

	auto alloc    = device->CreateCommandAllocator();
	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kGraphics;
	auto list     = device->CreateCommandList(listDesc, alloc, rm);

	auto bufDesc      = bgpu::RawViewDesc();
	bufDesc.byteSize  = c_CopyBytes;
	bufDesc.debugName = "flush idle probe";
	bufDesc.isUav     = true;

	const bgpu::BufferHandle src = rm->CreateRawBuffer(bufDesc);
	const bgpu::BufferHandle dst = rm->CreateRawBuffer(bufDesc);
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

		MTL::CommandBuffer* cmdBuffer = list->As<bgpu::CommandList>()->GetCommandBuffer();
		(void)queue->ExecuteCommandList(list.Get());

		queue->Flush();

		if (cmdBuffer->status() != MTL::CommandBufferStatusCompleted)
			++unretired;
	}

	CHECK(unretired == 0);

	rm->DestroyBuffer(dst, false);
	rm->DestroyBuffer(src, false);
	rm->UnregisterQueue(queue.Get());
	queue->Flush();
}

#endif
