// Held through SharedRef via `auto` and dereferenced: both need the complete type, which
// include-cleaner cannot see through the template.
#include <bgpu/GpuContext.h>
#include <bgpu/buffer/EntryBuffer.h>
#include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>   // IWYU pragma: keep
#include <bgpu/debug/DebugBuffer.h>  // IWYU pragma: keep
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

// What a compute client beside the renderer does, with nothing of the renderer in the process: this
// suite links bgpu and no renderer. Its own device on the shared context, its own resource manager with its
// own compute queue registered, the buffer family written from the CPU, one dispatch, one readback.
TEST_CASE("A compute owner dispatches and reads back without a renderer", "[compute][render]")
{
	auto contextDesc             = bgpu::GpuContextDesc();
	contextDesc.enableDebugLayer = true;
	auto context                 = bgpu::CreateGpuContext(contextDesc);
	REQUIRE(context != nullptr);

	auto device = bgpu::CreateDevice(context);
	REQUIRE(device != nullptr);

	auto rm = device->CreateResourceManager(bgpu::ResourceManagerDesc());
	REQUIRE(rm != nullptr);

	auto queue = device->CreateCommandQueue(bgpu::QueueType::kCompute);
	REQUIRE(queue != nullptr);
	rm->RegisterQueue(queue.Get());

	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kCompute;
	auto alloc    = device->CreateCommandAllocator(bgpu::QueueType::kCompute);
	auto list     = device->CreateCommandList(listDesc, alloc, rm);

	constexpr uint32_t c_Count = 8;

	auto valuesDesc         = bgpu::EntryBufferDesc();
	valuesDesc.initialCount = c_Count;
	valuesDesc.debugName    = "Compute-only values";
	auto values             = bgpu::EntryBuffer<uint32_t>(valuesDesc, rm);

	std::vector<core::slot_handle> handles;
	for (uint32_t i = 0; i < c_Count; ++i)
	{
		handles.push_back(values.EmplaceBack(i + 3u));
	}
	// The kernel addresses element i + 1 directly: pin that the arena hands them out in order.
	for (uint32_t i = 0; i < c_Count; ++i)
	{
		REQUIRE(handles[i].index == i + 1u);
	}

	auto outDesc = bgpu::ComputeBufferDesc();
	outDesc.SetElement<uint32_t>().SetInitialCount(c_Count).SetDebugName("Compute-only out");
	const bgpu::BufferHandle out = rm->CreateComputeBuffer(outDesc);
	REQUIRE(rm->ValidBufferHandle(out));

	auto kernel = device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(device->CreateShader("bgpu.CSEntrySquare"))
			.SetDebugName("bgpu.CSEntrySquare"));
	REQUIRE(kernel.pipeline != nullptr);

	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = c_Count * sizeof(uint32_t);
	rbDesc.debugName = "Compute-only readback";
	const auto rb    = rm->CreateReadbackBuffer(rbDesc);

	list->Open(queue.Get(), alloc.Get());

#if defined(BERNINI_GPU_DEBUG)
	// The buffer family asserts through gDebug, and the sessions define BERNINI_GPU_DEBUG for every
	// owner, so a second owner binds an assert buffer of its own exactly as the renderer does.
	auto debugBuffer = bgpu::DebugBuffer();
	debugBuffer.Init(16, rm);
	debugBuffer.Reset(list.Get());
	list->Barrier(
		debugBuffer.GetBufferHandle(),
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kUnorderedAccess));
	list->SetActiveDebugBuffer(debugBuffer.GetBufferHandle());
#endif

	values.Update(list.Get());
	list->Barrier(
		values.GetBufferHandle(),
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kShaderResource));

	kernel["gUniforms"]["values"]    = values.GetBufferHandle();
	kernel["gUniforms"]["outBuffer"] = out;

	auto state   = bgpu::ComputeState();
	state.kernel = &kernel;
	list->SetComputeState(state);
	list->Dispatch(1, 1, 1);

	list->Barrier(
		out,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));
	list->CopyBufferToReadback(rb, out);
	list->Close();

	queue->WaitForFenceCPUBlocking(queue->ExecuteCommandList(list));

	const auto* mapped = static_cast<const uint32_t*>(rm->MapReadback(rb));
	REQUIRE(mapped != nullptr);
	for (uint32_t i = 0; i < c_Count; ++i)
	{
		CHECK(mapped[i] == (i + 3u) * (i + 3u));
	}
	rm->UnmapReadback(rb);

	rm->DestroyReadbackBuffer(rb, false);
	rm->DestroyBuffer(out, false);
	values.Release(false);
#if defined(BERNINI_GPU_DEBUG)
	debugBuffer.Release(false);
#endif
	rm->UnregisterQueue(queue.Get());
}
