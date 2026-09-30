#include "gfx/GraphicsBase.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <bgl/IGraphics.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/DispatchArgs.h>
#include <bgpu/buffer/ComputeBuffer.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/uniforms/Uniforms.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

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
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc  = bgpu::CommandListDesc();
	cmdListDesc.type  = bgpu::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

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

	auto entries = bgpu::ComputeBuffer();
	{
		auto desc = bgpu::ComputeBufferDesc();
		desc.SetElement<SortEntry>().SetInitialCount(c_SortCapacity).SetDebugName("Sort Entries");
		entries.Init(desc, resourceManager);
	}

	auto counter = bgpu::ComputeBuffer();
	{
		auto desc = bgpu::ComputeBufferDesc();
		desc.SetElement<uint32_t>().SetInitialCount(1).SetDebugName("Sort Count");
		counter.Init(desc, resourceManager);
	}

	// Written alongside the sorted entries; this case only asserts on the entries, so these exist to
	// give the shader somewhere legal to write.
	auto sortedInstances = bgpu::ComputeBuffer();
	{
		auto desc = bgpu::ComputeBufferDesc();
		desc.SetElement<uint32_t>()
			.SetInitialCount(c_SortCapacity)
			.SetDebugName("Sorted Instances");
		sortedInstances.Init(desc, resourceManager);
	}

	auto dispatchArgs = bgpu::ComputeBuffer();
	{
		auto desc = bgpu::ComputeBufferDesc();
		desc.SetElement<bgl::idl::DispatchArgs>().SetInitialCount(1).SetDebugName("Dispatch Args");
		dispatchArgs.Init(desc, resourceManager);
	}

	auto kernel = device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(device->CreateShader("programs.culling.TransparentSort"))
			.SetDebugName("Transparent Sort"));

	kernel["gUniforms"]["entries"]         = entries.GetBufferHandle();
	kernel["gUniforms"]["count"]           = counter.GetBufferHandle();
	kernel["gUniforms"]["sortedInstances"] = sortedInstances.GetBufferHandle();
	kernel["gUniforms"]["dispatchArgs"]    = dispatchArgs.GetBufferHandle();

	cmdList->Open(cmdQueue.Get(), cmdAllocator.Get());

	cmdList->WriteBuffer(entries.GetBufferHandle(), input.data(), input.size() * sizeof(SortEntry));
	cmdList->WriteBuffer(counter.GetBufferHandle(), &c_Count, sizeof(c_Count));

	const auto bufferBarrier = [](bgpu::BarrierSyncFlag   syncBefore,
	                              bgpu::BarrierAccessFlag accessBefore,
	                              bgpu::BarrierSyncFlag   syncAfter,
	                              bgpu::BarrierAccessFlag accessAfter) {
		return bgpu::BufferBarrierDesc()
		    .AddSyncBefore(syncBefore)
		    .AddAccessBefore(accessBefore)
		    .AddSyncAfter(syncAfter)
		    .AddAccessAfter(accessAfter);
	};

	const auto toWrite = bufferBarrier(
		bgpu::BarrierSyncFlag::kCopy,
		bgpu::BarrierAccessFlag::kCopyDest,
		bgpu::BarrierSyncFlag::kComputeShader,
		bgpu::BarrierAccessFlag::kUnorderedAccess);

	cmdList->Barrier(entries.GetBufferHandle(), toWrite);
	cmdList->Barrier(counter.GetBufferHandle(), toWrite);

	auto state   = bgpu::ComputeState();
	state.kernel = &kernel;
	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		entries.GetBufferHandle(),
		bufferBarrier(
			bgpu::BarrierSyncFlag::kComputeShader,
			bgpu::BarrierAccessFlag::kUnorderedAccess,
			bgpu::BarrierSyncFlag::kCopy,
			bgpu::BarrierAccessFlag::kCopySource));

	auto readbackDesc      = bgpu::ReadbackBufferDesc();
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

	resourceManager->DestroyReadbackBuffer(readback, false);
}
