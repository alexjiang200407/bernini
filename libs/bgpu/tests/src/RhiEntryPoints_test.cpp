// Held through SharedRef via `auto` and dereferenced: both need the complete type, which
// include-cleaner cannot see through the template.
#include <array>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>   // IWYU pragma: keep
#include <bgpu/cmd/TimestampHeap.h>  // IWYU pragma: keep
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <bgpu/resource/NativeTextureDesc.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Color.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/MeshletState.h>
#include <bgpu/types/NativeObject.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/types/Viewport.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// Every factory and destroy the RHI offers, once each, with no renderer in the process. What it
// checks is that each works on a device nothing else owns; what it is for is coverage -- the
// autorelease check (scripts/tests/test_autorelease_pools.py) runs this suite and names anything
// left without a pool, and a path no case reaches is a path it cannot see. A new entry point gets a
// line in one of the two cases: the compute half, which every backend runs, or the graphics half,
// which Vulkan does not have yet.
TEST_CASE("Every compute RHI entry point runs on a device no renderer owns", "[render][rhi]")
{
	constexpr uint32_t c_Count = 16;

	auto contextDesc             = bgpu::GpuContextDesc();
	contextDesc.enableDebugLayer = true;
	// On D3D12 an object this case leaks, reported as the context dies, ends the process.
	contextDesc.strictError = true;
	auto context            = bgpu::CreateGpuContext(contextDesc);
	REQUIRE(context != nullptr);

	auto device = bgpu::CreateDevice(context);
	REQUIRE(device != nullptr);

	auto rm    = device->CreateResourceManager(bgpu::ResourceManagerDesc());
	auto queue = device->CreateCommandQueue(bgpu::QueueType::kGraphics);
	REQUIRE(rm != nullptr);
	REQUIRE(queue != nullptr);
	rm->RegisterQueue(queue.Get());
	(void)queue->GetTimestampFrequency();

	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kGraphics;
	auto alloc    = device->CreateCommandAllocator();
	auto list     = device->CreateCommandList(listDesc, alloc, rm);
	auto timing   = device->CreateTimestampHeap(2);

	// Buffers, and a second view of one, read-only and writable.
	const auto structBuffer = rm->CreateStructBuffer(
		bgpu::StructBufferDesc()
			.SetElement<uint32_t>()
			.SetElementCount(c_Count)
			.SetIsUav()
			.SetDebugName("entry points: struct"));
	const auto rawBuffer = rm->CreateRawBuffer(
		bgpu::RawViewDesc().SetByteSize(256).SetIsUav().SetDebugName("entry points: raw"));
	const auto computeBuffer = rm->CreateComputeBuffer(
		bgpu::ComputeBufferDesc().SetElement<uint32_t>().SetInitialCount(c_Count).SetDebugName(
			"entry points: compute"));
	const auto bufferSrv = rm->CreateBufferSrv(
		structBuffer,
		bgpu::BufferSrvDesc().SetElement<uint32_t>().SetDebugName("entry points: buffer view"));
	REQUIRE(rm->ValidBufferHandle(structBuffer));
	REQUIRE(rm->ValidBufferHandle(rawBuffer));
	REQUIRE(rm->ValidBufferHandle(computeBuffer));
	REQUIRE(rm->ValidBufferSrvHandle(bufferSrv));

	const auto readOnlyBuffer = rm->CreateStructBuffer(
		bgpu::StructBufferDesc()
			.SetElement<uint32_t>()
			.SetElementCount(c_Count)
			.SetAllowsUav()
			.SetDebugName("entry points: read-only, writable view"));
	const auto bufferUav = rm->CreateBufferUav(
		readOnlyBuffer,
		bgpu::BufferUavDesc().SetElement<uint32_t>().SetDebugName("entry points: writable view"));
	REQUIRE(rm->ValidBufferUavHandle(bufferUav));

	// A buffer exported and imported back, as a second owner would.
	auto exportedType = bgpu::NativeObjectType::kMtlBuffer;
	auto exported     = bgpu::NativeObject();
	for (const auto type : { bgpu::NativeObjectType::kMtlBuffer,
	                         bgpu::NativeObjectType::kD3D12Resource,
	                         bgpu::NativeObjectType::kVkBuffer })
	{
		if (!exported)
		{
			exportedType = type;
			exported     = rm->GetNativeBuffer(structBuffer, type);
		}
	}
	REQUIRE(exported);
	const auto importedBuffer = rm->ImportNativeBuffer(
		bgpu::NativeBufferDesc()
			.SetObject(exportedType, exported)
			.SetBuffer(bgpu::StructBufferDesc().SetElement<uint32_t>().SetElementCount(c_Count)));
	REQUIRE(rm->ValidBufferHandle(importedBuffer));

	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = c_Count * sizeof(uint32_t);
	rbDesc.debugName = "entry points: readback";
	const auto rb    = rm->CreateReadbackBuffer(rbDesc);

	// A compute kernel, dispatched once through the writable view, timed, and read back.
	auto kernel = device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(device->CreateShader("bgpu.CSWriteEntryRange"))
			.SetDebugName("entry points: compute kernel"));
	REQUIRE(kernel.pipeline != nullptr);
	kernel["gUniforms"]["values"] = bufferUav;
	kernel["gUniforms"]["first"]  = 0U;
	kernel["gUniforms"]["count"]  = c_Count;

	auto state   = bgpu::ComputeState();
	state.kernel = &kernel;

	list->Open(queue.Get(), alloc.Get());
	list->BeginEvent("entry points");
	if (timing != nullptr)
		list->BeginTiming(*timing, 0, 1);
	list->SetComputeState(state);
	list->Dispatch(c_Count / 8, 1, 1);
	if (timing != nullptr)
	{
		list->EndTiming();
		list->ResolveTimestamps(*timing, 0, 2);
	}
	list->EndEvent();
	list->Barrier(
		readOnlyBuffer,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));
	list->CopyBufferToReadback(rb, readOnlyBuffer);
	list->Close();
	queue->WaitForFenceCPUBlocking(queue->ExecuteCommandList(list.Get()));

	const auto* values = static_cast<const uint32_t*>(rm->MapReadback(rb));
	REQUIRE(values != nullptr);
	CHECK(values[c_Count - 1] == 100 + c_Count - 1);
	rm->UnmapReadback(rb);

	if (timing != nullptr)
	{
		std::array<uint64_t, 2> ticks{};
		timing->Read(0, ticks);
	}

	// Deferred destroys, reclaimed once the queue has passed them; then the immediate ones.
	rm->DestroyBufferSrv(bufferSrv);
	rm->DestroyBufferUav(bufferUav);
	rm->DestroyBuffer(readOnlyBuffer);
	rm->DestroyBuffer(importedBuffer);
	rm->DestroyBuffer(structBuffer);
	rm->DestroyBuffer(rawBuffer);
	queue->Flush();
	rm->CleanupExpiredResources();

	rm->DestroyBuffer(computeBuffer, false);
	rm->DestroyReadbackBuffer(rb, false);
	rm->UnregisterQueue(queue.Get());
}

TEST_CASE("Every graphics RHI entry point runs on a device no renderer owns", "[render][rhi]")
{
	constexpr uint32_t c_Size = 8;

	auto contextDesc             = bgpu::GpuContextDesc();
	contextDesc.enableDebugLayer = true;
	// On D3D12 an object this case leaks, reported as the context dies, ends the process.
	contextDesc.strictError = true;
	auto context            = bgpu::CreateGpuContext(contextDesc);
	REQUIRE(context != nullptr);

	auto device = bgpu::CreateDevice(context);
	REQUIRE(device != nullptr);

	auto rm    = device->CreateResourceManager(bgpu::ResourceManagerDesc());
	auto queue = device->CreateCommandQueue(bgpu::QueueType::kGraphics);
	REQUIRE(rm != nullptr);
	REQUIRE(queue != nullptr);
	rm->RegisterQueue(queue.Get());

	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kGraphics;
	auto alloc    = device->CreateCommandAllocator();
	auto list     = device->CreateCommandList(listDesc, alloc, rm);
	auto timing   = device->CreateTimestampHeap(2);

	// A colour target with its three views, a depth target, a sampler.
	auto colorDesc   = bgpu::TextureDesc();
	colorDesc.width  = c_Size;
	colorDesc.height = c_Size;
	colorDesc.format = bgpu::Format::RGBA8_UNORM;
	colorDesc.usage =
		bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget, bgpu::TextureUsageFlag::kSRV };
	colorDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
	colorDesc.debugName     = "entry points: color";
	colorDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));
	const auto color = rm->CreateTexture(colorDesc);

	auto srvDesc   = bgpu::SrvDesc();
	srvDesc.format = bgpu::Format::RGBA8_UNORM;
	auto rtvDesc   = bgpu::RtvDesc();
	rtvDesc.format = bgpu::Format::RGBA8_UNORM;
	const auto srv = rm->CreateSrv(color, srvDesc);
	const auto rtv = rm->CreateRtv(color, rtvDesc);

	auto depthDesc          = bgpu::TextureDesc();
	depthDesc.width         = c_Size;
	depthDesc.height        = c_Size;
	depthDesc.format        = bgpu::Format::D32;
	depthDesc.usage         = bgpu::TextureUsageFlag::kDepthStencil;
	depthDesc.initialLayout = bgpu::BarrierLayout::kDepthWrite;
	depthDesc.debugName     = "entry points: depth";
	depthDesc.clearValue.SetDepthStencil(1.0f, 0);
	const auto depth = rm->CreateTexture(depthDesc);

	auto dsvDesc   = bgpu::DsvDesc();
	dsvDesc.format = bgpu::Format::D32;
	const auto dsv = rm->CreateDsv(depth, dsvDesc);

	const auto sampler = rm->CreateSampler(bgpu::SamplerDesc());
	REQUIRE(rm->ValidTextureHandle(color));
	REQUIRE(rm->ValidSrvHandle(srv));
	REQUIRE(rm->ValidRtvHandle(rtv));
	REQUIRE(rm->ValidDsvHandle(dsv));
	REQUIRE(rm->ValidSamplerHandle(sampler));

	// A texture answers for exactly its backend's native kind, and is imported back as one where the
	// backend adopts textures: Metal does not, since a drawable is the renderer's own.
	auto exportedType = bgpu::NativeObjectType::kMtlTexture;
	auto exported     = bgpu::NativeObject();
	int  answered     = 0;
	for (const auto type : { bgpu::NativeObjectType::kMtlTexture,
	                         bgpu::NativeObjectType::kD3D12Resource,
	                         bgpu::NativeObjectType::kVkImage })
	{
		if (const auto object = rm->GetNativeTexture(color, type))
		{
			exportedType = type;
			exported     = object;
			++answered;
		}
	}
	REQUIRE(answered == 1);
	auto importedDesc          = colorDesc;
	importedDesc.debugName     = "entry points: imported color";
	const auto importedTexture = rm->ImportNativeTexture(
		bgpu::NativeTextureDesc().SetObject(exportedType, exported).SetTexture(importedDesc));
	REQUIRE(
		rm->ValidTextureHandle(importedTexture) ==
		(exportedType != bgpu::NativeObjectType::kMtlTexture));

	const auto layout = rm->GetTextureReadbackLayout(color);
	auto       rbDesc = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize   = layout.totalBytes;
	rbDesc.debugName  = "entry points: readback";
	const auto rb     = rm->CreateReadbackBuffer(rbDesc);

	// A meshlet pipeline, drawn once into both targets, timed, and read back.
	auto kernel = device->CreateMeshletKernel(
		bgpu::MeshletPipelineDesc()
			.SetMeshShader(device->CreateShader("bgpu.MeshTriangle", "MSMain"))
			.SetPixelShader(device->CreateShader("bgpu.MeshTriangle", "PSMain"))
			.AddRtvFormat(bgpu::Format::RGBA8_UNORM)
			.SetDsvFormat(bgpu::Format::D32));
	REQUIRE(kernel.pipeline != nullptr);

	auto state   = bgpu::MeshletState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgpu::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size)));
	state.frameBuffer.AddColorAttachment(rtv);
	state.frameBuffer.SetDepthAttachment(dsv);

	list->Open(queue.Get(), alloc.Get());
	float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	rm->ClearRtv(list.Get(), rtv, clear);
	rm->ClearDsv(list.Get(), dsv, 1.0f, 0);
	if (timing != nullptr)
		list->BeginTiming(*timing, 0, 1);
	list->SetMeshletState(state);
	list->DispatchMesh(1, 1, 1);
	if (timing != nullptr)
		list->EndTiming();
	list->Barrier(
		color,
		bgpu::TextureBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kRenderTarget)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kRenderTarget)
			.SetLayoutBefore(bgpu::BarrierLayout::kRenderTarget)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
			.SetLayoutAfter(bgpu::BarrierLayout::kCopySource));
	list->CopyTextureToReadback(rb, color);
	list->Close();
	queue->WaitForFenceCPUBlocking(queue->ExecuteCommandList(list.Get()));

	const auto* texels = static_cast<const uint8_t*>(rm->MapReadback(rb));
	REQUIRE(texels != nullptr);
	CHECK(texels[layout.offset] == 255);  // the triangle's red, where the clear left zero
	rm->UnmapReadback(rb);

	if (timing != nullptr)
	{
		std::array<uint64_t, 2> ticks{};
		timing->Read(0, ticks);
	}

	// Deferred destroys, reclaimed once the queue has passed them; then the immediate ones.
	rm->DestroySrv(srv);
	rm->DestroyRtv(rtv);
	if (!importedTexture.IsNull())
		rm->DestroyTexture(importedTexture);
	rm->DestroyTexture(color);
	queue->Flush();
	rm->CleanupExpiredResources();

	rm->DestroyDsv(dsv, false);
	rm->DestroyTexture(depth, false);
	rm->DestroySampler(sampler, false);
	rm->DestroyReadbackBuffer(rb, false);
	rm->UnregisterQueue(queue.Get());
}
