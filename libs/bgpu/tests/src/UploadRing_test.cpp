// The ring is D3D12's: Metal stages each write in a buffer of its own, which the command buffer
// retains until it completes.
#if defined(RENDERER_BACKEND_DX12)

// Held through SharedRef via `auto` and dereferenced: both need the complete type, which
// include-cleaner cannot see through the template.
#	include <bgpu/GpuContext.h>
#	include <bgpu/MemoryTag.h>
#	include <bgpu/buffer/EntryBuffer.h>
#	include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#	include <bgpu/cmd/CommandList.h>
#	include <bgpu/cmd/CommandQueue.h>  // IWYU pragma: keep
#	include <bgpu/device/Device.h>
#	include <bgpu/resource/ResourceManager.h>
#	include <bgpu/types/QueueType.h>
#	include <catch2/catch_test_macros.hpp>
#	include <core/profiling/memory.h>
#	include <cstdint>

// The ring keeps every chunk it made until its list dies, so its cost is the list's peak upload.
// Charged, it leaves the untagged residual; uncharged, a growing ring looked like a missing tag.
TEST_CASE("The upload ring's chunks are charged to device buffer", "[compute][render][memory]")
{
	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	auto device  = bgpu::CreateDevice(context);
	auto rm      = device->CreateResourceManager(bgpu::ResourceManagerDesc());
	auto queue   = device->CreateCommandQueue(bgpu::QueueType::kCompute);
	rm->RegisterQueue(queue.Get());

	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kCompute;

	{
		auto alloc = device->CreateCommandAllocator(bgpu::QueueType::kCompute);
		auto list  = device->CreateCommandList(listDesc, alloc, rm);

		auto values = bgpu::EntryBuffer<uint32_t>(
			rm,
			bgpu::EntryBufferDesc().SetInitialCount(16).SetDebugName("Upload ring values"));
		// Fewer than its capacity, so Update writes through the ring and never regrows the buffer.
		for (uint32_t i = 0; i < 8; ++i)
		{
			values.EmplaceBack(i);
		}

		const uint64_t before = core::profiling::tag_totals(bgpu::MemoryTag::kDeviceBuffer).live;

		list->Open(queue.Get(), alloc.Get());
		values.Update(list.Get());
		list->Close();
		queue->WaitForFenceCPUBlocking(queue->ExecuteCommandList(list));

		CHECK(
			core::profiling::tag_totals(bgpu::MemoryTag::kDeviceBuffer).live >=
			before + listDesc.uploadChunkSize);
	}

	queue->Flush();
	rm->UnregisterQueue(queue.Get());
}

#endif
