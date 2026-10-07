// Held through SharedRef via `auto` and dereferenced: both need the complete type, which
// include-cleaner cannot see through the template.
#include <algorithm>
#include <array>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>  // IWYU pragma: keep
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/NativeObject.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace
{
	constexpr auto c_BufferKinds = std::to_array(
		{ bgpu::NativeObjectType::kMtlBuffer,
	      bgpu::NativeObjectType::kD3D12Resource,
	      bgpu::NativeObjectType::kVkBuffer });

	// The buffer kind this backend answers for; only one of them is non-null.
	struct NativeBuffer
	{
		bgpu::NativeObjectType type{};
		bgpu::NativeObject     object;
	};

	NativeBuffer
	ExportBuffer(const bgpu::IResourceManager& rm, bgpu::BufferHandle buffer)
	{
		for (const auto type : c_BufferKinds)
		{
			if (const auto object = rm.GetNativeBuffer(buffer, type))
				return NativeBuffer{ type, object };
		}
		return {};
	}
}

// Two owners on one context, as a crowd and a renderer are: A writes a buffer on its queue, B
// imports it and reads it on its own after a GPU-side wait on A's. B is submitted first, so only
// the wait stands between its read and A's write.
TEST_CASE(
	"An imported buffer reads what its producer wrote, after a wait on its queue",
	"[compute][render][import]")
{
	constexpr uint32_t c_Count = 8;

	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	REQUIRE(context != nullptr);

	auto deviceA = bgpu::CreateDevice(context);
	auto rmA     = deviceA->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly());
	auto queueA  = deviceA->CreateCommandQueue(bgpu::QueueType::kCompute);
	rmA->RegisterQueue(queueA.Get());

	auto deviceB = bgpu::CreateDevice(context);
	auto rmB     = deviceB->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly());
	auto queueB  = deviceB->CreateCommandQueue(bgpu::QueueType::kCompute);
	rmB->RegisterQueue(queueB.Get());

	const bgpu::BufferHandle produced = rmA->CreateStructBuffer(
		bgpu::StructBufferDesc()
			.SetElement<uint32_t>()
			.SetElementCount(c_Count)
			.SetIsUav()
			.SetDebugName("Import: produced"));
	REQUIRE(rmA->ValidBufferHandle(produced));

	const NativeBuffer exported = ExportBuffer(*rmA, produced);
	REQUIRE(exported.object);
	CHECK_FALSE(rmA->GetNativeBuffer(produced, bgpu::NativeObjectType::kMtlTexture));

	const bgpu::BufferHandle imported = rmB->ImportNativeBuffer(
		bgpu::NativeBufferDesc()
			.SetObject(exported.type, exported.object)
			.SetBuffer(
				bgpu::StructBufferDesc()
					.SetElement<uint32_t>()
					.SetElementCount(c_Count)
					.SetDebugName("Import: imported")));
	REQUIRE(rmB->ValidBufferHandle(imported));
	CHECK(rmB->GetNativeBuffer(imported, exported.type).pointer == exported.object.pointer);

	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = c_Count * sizeof(uint32_t);
	rbDesc.debugName = "Import: readback";
	const auto rb    = rmB->CreateReadbackBuffer(rbDesc);

	// B: wait for A's next submission, then copy what it wrote.
	const uint64_t written = queueA->GetNextFenceValue();
	queueB->InsertWaitForQueueFence(queueA.Get(), written);

	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kCompute;
	auto allocB   = deviceB->CreateCommandAllocator(bgpu::QueueType::kCompute);
	auto listB    = deviceB->CreateCommandList(listDesc, allocB, rmB);
	listB->Open(queueB.Get(), allocB.Get());
	listB->CopyBufferToReadback(rb, imported);
	listB->Close();
	const uint64_t read = queueB->ExecuteCommandList(listB);

	// A: write 100 + i into every element.
	auto kernel = deviceA->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(deviceA->CreateShader("bgpu.CSWriteEntryRange"))
			.SetDebugName("bgpu.CSWriteEntryRange"));
	REQUIRE(kernel.pipeline != nullptr);
	kernel["gUniforms"]["values"] = produced;
	kernel["gUniforms"]["first"]  = 0u;
	kernel["gUniforms"]["count"]  = c_Count;

	auto allocA = deviceA->CreateCommandAllocator(bgpu::QueueType::kCompute);
	auto listA  = deviceA->CreateCommandList(listDesc, allocA, rmA);
	listA->Open(queueA.Get(), allocA.Get());
	auto state   = bgpu::ComputeState();
	state.kernel = &kernel;
	listA->SetComputeState(state);
	listA->Dispatch(1, 1, 1);
	listA->Barrier(
		produced,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));
	listA->Close();
	CHECK(queueA->ExecuteCommandList(listA) == written);

	queueB->WaitForFenceCPUBlocking(read);

	const auto* mapped = static_cast<const uint32_t*>(rmB->MapReadback(rb));
	REQUIRE(mapped != nullptr);
	for (uint32_t i = 0; i < c_Count; ++i)
	{
		CHECK(mapped[i] == 100u + i);
	}
	rmB->UnmapReadback(rb);

	// The import holds a reference of its own: the producer releasing first frees nothing B reads.
	rmA->DestroyBuffer(produced);
	queueA->Flush();
	CHECK(rmB->ValidBufferHandle(imported));
	CHECK(rmB->GetNativeBuffer(imported, exported.type));

	rmB->DestroyReadbackBuffer(rb, false);
	rmB->DestroyBuffer(imported);
	queueB->Flush();
	rmB->UnregisterQueue(queueB.Get());
	rmA->UnregisterQueue(queueA.Get());
}

TEST_CASE("An import refuses an object of a kind this backend does not adopt", "[import]")
{
	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	auto device  = bgpu::CreateDevice(context);
	auto rm      = device->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly());

	uint32_t notABuffer = 0;
	CHECK(rm->ImportNativeBuffer(bgpu::NativeBufferDesc()).IsNull());
	CHECK(
		rm->ImportNativeBuffer(
			  bgpu::NativeBufferDesc()
				  .SetObject(bgpu::NativeObjectType::kMtlTexture, bgpu::NativeObject{ &notABuffer })
				  .SetBuffer(bgpu::StructBufferDesc().SetElement<uint32_t>().SetElementCount(1)))
			.IsNull());
	CHECK_FALSE(rm->GetNativeBuffer(bgpu::BufferHandle{}, bgpu::NativeObjectType::kMtlBuffer));
}

// What another owner asks for is the one kind its backend has, so a buffer answers for exactly one.
TEST_CASE("A buffer is exported as its backend's one native kind", "[import]")
{
	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	auto device  = bgpu::CreateDevice(context);
	auto rm      = device->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly());

	const auto buffer = rm->CreateStructBuffer(
		bgpu::StructBufferDesc().SetElement<uint32_t>().SetElementCount(4).SetDebugName(
			"export kinds"));
	REQUIRE(rm->ValidBufferHandle(buffer));

	CHECK(std::ranges::count_if(c_BufferKinds, [&](const bgpu::NativeObjectType type) {
			  return static_cast<bool>(rm->GetNativeBuffer(buffer, type));
		  }) == 1);

	rm->DestroyBuffer(buffer, false);
}
