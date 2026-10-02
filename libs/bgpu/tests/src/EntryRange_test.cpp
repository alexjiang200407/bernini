// Held through SharedRef via `auto` and dereferenced: both need the complete type, which
// include-cleaner cannot see through the template.
#include <bgpu/GpuContext.h>
#include <bgpu/buffer/EntryBuffer.h>
#include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>  // IWYU pragma: keep
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_test_macros.hpp>
#include <core/containers/slot_handle.h>
#include <cstdint>
#include <vector>

// A claimed range is the part of an EntryBuffer the CPU writes once and a pass writes every frame:
// it starts and ends on an upload block, the writable view reaches the buffer's own bytes, and what
// the GPU wrote there survives a growth, whose forward copy carries it while the CPU elements
// beside it upload as before.
TEST_CASE(
	"An EntryBuffer range is written through its writable view and survives growth",
	"[compute][render][entry_range]")
{
	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	REQUIRE(context != nullptr);
	auto device = bgpu::CreateDevice(context);
	REQUIRE(device != nullptr);
	auto rm = device->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly());
	REQUIRE(rm != nullptr);
	auto queue = device->CreateCommandQueue(bgpu::QueueType::kCompute);
	REQUIRE(queue != nullptr);
	rm->RegisterQueue(queue.Get());

	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kCompute;
	auto alloc    = device->CreateCommandAllocator(bgpu::QueueType::kCompute);
	auto list     = device->CreateCommandList(listDesc, alloc, rm);

	constexpr uint32_t c_PerBlock = 4;
	constexpr uint32_t c_Range    = 6;

	auto values = bgpu::EntryBuffer<uint32_t>(
		rm,
		bgpu::EntryBufferDesc()
			.SetInitialCount(5)
			.SetBlockSize(c_PerBlock * sizeof(uint32_t))
			.SetWritableView()
			.SetDebugName("Entry range values"));

	const core::slot_handle cpu    = values.Add(7u);
	const uint32_t          before = values.Capacity();

	const bgpu::EntryRange range = values.ClaimRange(c_Range, 0u);
	CHECK(range.count == c_Range);
	CHECK(range.first % c_PerBlock == 0);
	CHECK(range.first >= before);
	CHECK(values.Capacity() >= range.first + c_Range);

	const bgpu::BufferUavHandle firstView = values.GetWritableView();
	REQUIRE_FALSE(firstView.IsNull());

	auto kernel = device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(device->CreateShader("bgpu.CSWriteEntryRange"))
			.SetDebugName("bgpu.CSWriteEntryRange"));
	REQUIRE(kernel.pipeline != nullptr);

	list->Open(queue.Get(), alloc.Get());
	values.Update(list.Get());
	list->Barrier(
		values.GetBufferHandle(),
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kUnorderedAccess));

	kernel["gUniforms"]["values"] = values.GetWritableView();
	kernel["gUniforms"]["first"]  = range.first;
	kernel["gUniforms"]["count"]  = range.count;
	auto state                    = bgpu::ComputeState();
	state.kernel                  = &kernel;
	list->SetComputeState(state);
	list->Dispatch(1, 1, 1);

	list->Barrier(
		values.GetBufferHandle(),
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));
	list->Close();
	queue->WaitForFenceCPUBlocking(queue->ExecuteCommandList(list));

	// Fills every free index below the range, then grows past it.
	std::vector<core::slot_handle> added;
	const uint32_t                 claimed = values.Capacity();
	while (values.Capacity() == claimed)
	{
		added.push_back(values.Add(9u));
	}
	for (const core::slot_handle& handle : added)
	{
		CHECK((handle.index < range.first || handle.index >= range.first + c_PerBlock * 2));
	}

	const bgpu::BufferUavHandle grownView = values.GetWritableView();
	REQUIRE_FALSE(grownView.IsNull());
	CHECK(grownView.bindlessIndex != firstView.bindlessIndex);

	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = static_cast<uint64_t>(values.Capacity()) * sizeof(uint32_t);
	rbDesc.debugName = "Entry range readback";
	const auto rb    = rm->CreateReadbackBuffer(rbDesc);

	list->Open(queue.Get(), alloc.Get());
	values.Update(list.Get());
	list->Barrier(
		values.GetBufferHandle(),
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));
	list->CopyBufferToReadback(rb, values.GetBufferHandle());
	list->Close();
	queue->WaitForFenceCPUBlocking(queue->ExecuteCommandList(list));

	const auto* mapped = static_cast<const uint32_t*>(rm->MapReadback(rb));
	REQUIRE(mapped != nullptr);
	CHECK(mapped[cpu.index] == 7u);
	for (uint32_t i = 0; i < c_Range; ++i)
	{
		CHECK(mapped[range.first + i] == 100u + i);
	}
	for (const core::slot_handle& handle : added)
	{
		CHECK(mapped[handle.index] == 9u);
	}
	rm->UnmapReadback(rb);

	values.ReleaseRange(range);
	const core::slot_handle reused = values.Add(11u);
	CHECK_FALSE(reused.is_null());

	rm->DestroyReadbackBuffer(rb, false);
	queue->Flush();
	rm->UnregisterQueue(queue.Get());
}

TEST_CASE("An EntryBuffer refuses to release what it did not claim", "[entry_range]")
{
	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	auto device  = bgpu::CreateDevice(context);
	auto rm      = device->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly());

	auto values = bgpu::EntryBuffer<uint32_t>(
		rm,
		bgpu::EntryBufferDesc()
			.SetInitialCount(3)
			.SetBlockSize(4 * sizeof(uint32_t))
			.SetDebugName("Entry range refusals"));

	CHECK(values.GetWritableView().IsNull());

	const bgpu::EntryRange range = values.ClaimRange(4, 0u);
	CHECK_THROWS(values.ReleaseRange({ range.first + 1, 3 }));
	CHECK_THROWS(values.ReleaseRange({ range.first, 0 }));
	CHECK_THROWS(values.ReleaseRange({ range.first + 4, 4 }));
	CHECK_NOTHROW(values.ReleaseRange(range));
	CHECK_THROWS(values.ReleaseRange(range));
}
