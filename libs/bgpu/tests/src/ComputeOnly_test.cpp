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
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_test_macros.hpp>
#include <core/containers/slot_handle.h>
#include <cstdint>
#include <vector>

#if !defined(RENDERER_BACKEND_VULKAN)
#	include <bgpu/resource/Dsv.h>
#	include <bgpu/resource/Rtv.h>
#	include <bgpu/resource/Srv.h>
#endif

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

	auto rm = device->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly());
	REQUIRE(rm != nullptr);

	auto queue = device->CreateCommandQueue(bgpu::QueueType::kCompute);
	REQUIRE(queue != nullptr);
	rm->RegisterQueue(queue.Get());

	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kCompute;
	auto alloc    = device->CreateCommandAllocator(bgpu::QueueType::kCompute);
	auto list     = device->CreateCommandList(listDesc, alloc, rm);

	constexpr uint32_t c_Count = 8;

	auto values = bgpu::EntryBuffer<uint32_t>(
		rm,
		bgpu::EntryBufferDesc().SetInitialCount(c_Count).SetDebugName("Compute-only values"));

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
	auto debugBuffer = bgpu::DebugBuffer(rm, 16);
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
	// An owner drains the queues it made before it lets go of them (docs/bgpu.md, Teardown).
	queue->Flush();
	rm->UnregisterQueue(queue.Get());
}

// A pool of zero is a kind the owner makes none of: the manager is built without its heap, and a
// create from it fails as an exhausted pool does rather than touching a heap that does not exist.
TEST_CASE("A resource pool of zero refuses every create from it", "[compute][render]")
{
	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	auto device  = bgpu::CreateDevice(context);

	SECTION("no textures")
	{
		auto rm = device->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly());
		REQUIRE(rm != nullptr);

		auto textureDesc   = bgpu::TextureDesc();
		textureDesc.format = bgpu::Format::RGBA8_UNORM;
		CHECK_FALSE(rm->ValidTextureHandle(rm->CreateTexture(textureDesc)));
		CHECK_FALSE(rm->ValidSamplerHandle(rm->CreateSampler(bgpu::SamplerDesc())));

		auto bufferDesc = bgpu::ComputeBufferDesc();
		bufferDesc.SetElement<uint32_t>().SetInitialCount(4).SetDebugName("Zero-pool buffer");
		const bgpu::BufferHandle buffer = rm->CreateComputeBuffer(bufferDesc);
		CHECK(rm->ValidBufferHandle(buffer));
		rm->DestroyBuffer(buffer, false);
	}

	// Vulkan has no textures yet, so a pool that holds some is one it cannot fill.
#if !defined(RENDERER_BACKEND_VULKAN)
	SECTION("textures, but no view, target, depth or readback")
	{
		auto desc               = bgpu::ResourceManagerDesc::ComputeOnly();
		desc.maxTextures        = 3;
		desc.maxReadbackBuffers = 0;
		auto rm                 = device->CreateResourceManager(desc);
		REQUIRE(rm != nullptr);

		auto colorDesc   = bgpu::TextureDesc();
		colorDesc.format = bgpu::Format::RGBA8_UNORM;
		colorDesc.usage  = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kSRV,
			                                   bgpu::TextureUsageFlag::kRenderTarget };
		const auto color = rm->CreateTexture(colorDesc);
		REQUIRE(rm->ValidTextureHandle(color));

		auto depthDesc   = bgpu::TextureDesc();
		depthDesc.format = bgpu::Format::D32;
		depthDesc.usage  = bgpu::TextureUsageFlag::kDepthStencil;
		depthDesc.clearValue.SetDepthStencil(1.0f, 0);
		const auto depth = rm->CreateTexture(depthDesc);
		REQUIRE(rm->ValidTextureHandle(depth));

		auto srvDesc   = bgpu::SrvDesc();
		srvDesc.format = bgpu::Format::RGBA8_UNORM;
		CHECK_FALSE(rm->ValidSrvHandle(rm->CreateSrv(color, srvDesc)));

		auto rtvDesc   = bgpu::RtvDesc();
		rtvDesc.format = bgpu::Format::RGBA8_UNORM;
		CHECK_FALSE(rm->ValidRtvHandle(rm->CreateRtv(color, rtvDesc)));

		auto dsvDesc   = bgpu::DsvDesc();
		dsvDesc.format = bgpu::Format::D32;
		CHECK_FALSE(rm->ValidDsvHandle(rm->CreateDsv(depth, dsvDesc)));

		auto readbackDesc     = bgpu::ReadbackBufferDesc();
		readbackDesc.byteSize = 16;
		CHECK_FALSE(rm->ValidReadbackBufferHandle(rm->CreateReadbackBuffer(readbackDesc)));

		rm->DestroyTexture(color, false);
		rm->DestroyTexture(depth, false);
	}
#endif
}
