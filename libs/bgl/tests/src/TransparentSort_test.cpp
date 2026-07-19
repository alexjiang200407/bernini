#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "idl/Constants.h"
#include "idl/DispatchArgs.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/ComputeBuffer.h"
#include "types/ComputeState.h"
#include "uniforms/Uniforms.h"
#include "util/GpuValidation.h"
#include <bgl/IGraphics.h>

namespace
{
	constexpr uint32_t c_SortCapacity = bgl::idl::cTransparentSortCapacity;

	struct SortEntry
	{
		uint32_t key;
		uint32_t instance;
	};
}

// Sorts the (key, instance) pairs the depth-key pass produces. The payload has to travel with its
// key -- a sort that ordered the keys but shuffled the instance indices would draw the right depths
// in the wrong order, which no key-only check would catch. A count that is not a power of two is
// used so the padding path is exercised rather than assumed.
TEST_CASE(
	"Transparent sort orders entries by key and carries the payload",
	"[compute][transparentsort]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	// Deliberately not a power of two, and deliberately not already sorted. Each key is paired with
	// an instance derived from it, so the pairing can be checked after the shuffle.
	constexpr uint32_t c_Count = 613;

	std::vector<SortEntry> input(c_Count);
	for (uint32_t i = 0; i < c_Count; ++i)
	{
		// A stride coprime with the count walks every value exactly once in a scattered order.
		const uint32_t key = (i * 2654435761u) % 1000003u;
		input[i]           = SortEntry{ key, key ^ 0xA5A5A5A5u };
	}

	auto entries = bgl::ComputeBuffer();
	{
		auto desc = bgl::ComputeBufferDesc();
		desc.SetElement<SortEntry>().SetMaxCount(c_SortCapacity).SetDebugName("Sort Entries");
		entries.Init(desc, resourceManager);
	}

	auto counter = bgl::ComputeBuffer();
	{
		auto desc = bgl::ComputeBufferDesc();
		desc.SetElement<uint32_t>().SetMaxCount(1).SetDebugName("Sort Count");
		counter.Init(desc, resourceManager);
	}

	// Written alongside the sorted entries; this case only asserts on the entries, so these exist to
	// give the shader somewhere legal to write.
	auto sortedInstances = bgl::ComputeBuffer();
	{
		auto desc = bgl::ComputeBufferDesc();
		desc.SetElement<uint32_t>().SetMaxCount(c_SortCapacity).SetDebugName("Sorted Instances");
		sortedInstances.Init(desc, resourceManager);
	}

	auto partitionBase = bgl::ComputeBuffer();
	{
		auto desc = bgl::ComputeBufferDesc();
		desc.SetElement<uint32_t>()
			.SetMaxCount(bgl::idl::cTransparentPartitionCount)
			.SetDebugName("Partition Base");
		partitionBase.Init(desc, resourceManager);
	}

	auto partitionArgs = bgl::ComputeBuffer();
	{
		auto desc = bgl::ComputeBufferDesc();
		desc.SetElement<bgl::idl::DispatchArgs>()
			.SetMaxCount(bgl::idl::cTransparentPartitionCount)
			.SetDebugName("Partition Dispatch Args");
		partitionArgs.Init(desc, resourceManager);
	}

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("TransparentSort"))
			.SetDebugName("Transparent Sort"));

	kernel["gUniforms"]["entries"]               = entries.GetBufferHandle();
	kernel["gUniforms"]["count"]                 = counter.GetBufferHandle();
	kernel["gUniforms"]["sortedInstances"]       = sortedInstances.GetBufferHandle();
	kernel["gUniforms"]["partitionBase"]         = partitionBase.GetBufferHandle();
	kernel["gUniforms"]["partitionDispatchArgs"] = partitionArgs.GetBufferHandle();

	cmdList->Open(cmdQueue.Get(), cmdAllocator.Get());

	cmdList->WriteBuffer(entries.GetBufferHandle(), input.data(), input.size() * sizeof(SortEntry));
	cmdList->WriteBuffer(counter.GetBufferHandle(), &c_Count, sizeof(c_Count));

	const auto bufferBarrier = [](bgl::BarrierSyncFlag   syncBefore,
	                              bgl::BarrierAccessFlag accessBefore,
	                              bgl::BarrierSyncFlag   syncAfter,
	                              bgl::BarrierAccessFlag accessAfter) {
		return bgl::BufferBarrierDesc()
		    .AddSyncBefore(syncBefore)
		    .AddAccessBefore(accessBefore)
		    .AddSyncAfter(syncAfter)
		    .AddAccessAfter(accessAfter);
	};

	const auto toWrite = bufferBarrier(
		bgl::BarrierSyncFlag::kCopy,
		bgl::BarrierAccessFlag::kCopyDest,
		bgl::BarrierSyncFlag::kComputeShader,
		bgl::BarrierAccessFlag::kUnorderedAccess);

	cmdList->Barrier(entries.GetBufferHandle(), toWrite);
	cmdList->Barrier(counter.GetBufferHandle(), toWrite);

	auto state   = bgl::ComputeState();
	state.kernel = &kernel;
	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		entries.GetBufferHandle(),
		bufferBarrier(
			bgl::BarrierSyncFlag::kComputeShader,
			bgl::BarrierAccessFlag::kUnorderedAccess,
			bgl::BarrierSyncFlag::kCopy,
			bgl::BarrierAccessFlag::kCopySource));

	auto readbackDesc      = bgl::ReadbackBufferDesc();
	readbackDesc.byteSize  = sizeof(SortEntry) * c_SortCapacity;
	readbackDesc.debugName = "Sort Readback";
	auto readback          = resourceManager->CreateReadbackBuffer(readbackDesc);

	cmdList->CopyBufferToReadback(readback, entries.GetBufferHandle());

	cmdList->Close();
	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList.Get()));

	std::vector<SortEntry> got(c_Count);
	std::memcpy(got.data(), resourceManager->MapReadback(readback), sizeof(SortEntry) * c_Count);
	resourceManager->UnmapReadback(readback);

	for (uint32_t i = 1; i < c_Count; ++i)
	{
		INFO("entry " << i << " key " << got[i].key << " follows " << got[i - 1].key);
		CHECK(got[i - 1].key <= got[i].key);
	}

	// The payload must still belong to its key, and the multiset must be exactly what went in --
	// so nothing was dropped, duplicated, or overwritten by the padding.
	std::vector<uint32_t> gotKeys;
	gotKeys.reserve(c_Count);
	for (const SortEntry& entry : got)
	{
		CHECK(entry.instance == (entry.key ^ 0xA5A5A5A5u));
		gotKeys.push_back(entry.key);
	}

	std::vector<uint32_t> wantKeys;
	wantKeys.reserve(c_Count);
	for (const SortEntry& entry : input) wantKeys.push_back(entry.key);

	std::ranges::sort(wantKeys);
	std::ranges::sort(gotKeys);
	CHECK(gotKeys == wantKeys);

	resourceManager->DestroyReadbackBuffer(readback, 0, false);
}
