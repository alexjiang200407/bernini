#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "debug/DebugBuffer.h"
#include "debug/DebugReadback.h"
#include "gfx/GraphicsBase.h"
#include "idl/ErrorCode.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"
#include <bgl/IGraphics.h>

#if defined(BERNINI_GPU_DEBUG)

// End-to-end proof that the bindless-texture path works: upload a known non-zero texel
// into a 1x1 SRV texture, sample it in a compute shader, and raise a GPU assertion if
// the sampled color is non-null. A fired assertion (read back on the CPU) means the SRV
// resolved via handle.idx and the WriteTexture upload landed; no assertion would mean the
// bindless sample returned zero.
TEST_CASE(
	"bindless texture sample uploads and resolves end-to-end",
	"[texture][gpu-assert][compute]")
{
	constexpr uint32_t kCapacity = 16;

	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = true;

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

	auto debugBuffer = bgl::DebugBuffer();
	debugBuffer.Init(kCapacity, resourceManager);

	// A 1x1 RGBA8 texture we will upload a known red texel into.
	auto texDesc         = bgl::TextureDesc();
	texDesc.width        = 1;
	texDesc.height       = 1;
	texDesc.format       = bgl::Format::RGBA8_UNORM;
	texDesc.usage        = bgl::TextureUsageFlag::kSRV;
	texDesc.initalLayout = bgl::BarrierLayout::kCopyDest;
	texDesc.debugName    = "Texture Sample Test";

	auto texture = resourceManager->CreateTexture(texDesc);
	REQUIRE(resourceManager->ValidTextureHandle(texture));

	const uint8_t               redTexel[4] = { 255, 0, 0, 255 };
	bgl::TextureSubresourceData sub{};
	sub.data       = redTexel;
	sub.rowPitch   = sizeof(redTexel);
	sub.slicePitch = sizeof(redTexel);
	std::array<bgl::TextureSubresourceData, 1> subresources{ sub };

	// SamplerDesc defaults are linear filtering + clamp addressing.
	auto sampler = resourceManager->CreateSampler(bgl::SamplerDesc());

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(
				device->CreateShader("shaders/CSTextureSampleTest.dxil", "CSTextureSampleTest"))
			.SetDebugName("Texture Sample Test"));
	REQUIRE(kernel.pipeline != nullptr);
	REQUIRE(kernel.uniforms.contains("gUniforms"));
	REQUIRE(kernel.uniforms.contains("gDebug"));

	// Bind the bindless indices: for an SRV texture, handle.idx IS the descriptor index.
	kernel["gUniforms"]["texture"]["index"] = static_cast<uint32_t>(texture.idx);
	kernel["gUniforms"]["sampler"]["index"] = static_cast<uint32_t>(sampler.idx);

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = debugBuffer.ByteSize();
	rbDesc.debugName = "Texture Sample Readback";
	auto rb          = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	// Upload the texel, then transition the texture from copy-dest to shader-resource.
	cmdList->WriteTexture(texture, subresources);
	cmdList->Barrier(
		texture,
		bgl::TextureBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.SetLayoutBefore(bgl::BarrierLayout::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgl::BarrierAccessFlag::kShaderResource)
			.SetLayoutAfter(bgl::BarrierLayout::kShaderResource));

	// Reset the debug header, then hand the buffer to the shader.
	debugBuffer.Reset(cmdList);
	cmdList->Barrier(
		debugBuffer.GetBufferHandle(),
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgl::BarrierAccessFlag::kUnorderedAccess));

	auto computeState   = bgl::ComputeState();
	computeState.kernel = &kernel;
	cmdList->SetComputeState(computeState);
	cmdList->SetActiveDebugBuffer(debugBuffer.GetBufferHandle());
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		debugBuffer.GetBufferHandle(),
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));
	cmdList->CopyBufferToReadback(rb, debugBuffer.GetBufferHandle());

	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = resourceManager->MapReadback(rb);
	REQUIRE(mapped != nullptr);

	const auto report = bgl::InspectDebugReadback(mapped, kCapacity);
	REQUIRE(report.has_value());
	CHECK(report->count == 1);
	CHECK_FALSE(report->overflow);
	REQUIRE(report->records.size() == 1);
	CHECK(report->records[0].errcode == static_cast<uint32_t>(bgl::idl::ErrorCode::kUnknown));

	resourceManager->UnmapReadback(rb);

	debugBuffer.Release(fence, false);
	resourceManager->DestroyReadbackBuffer(rb, fence, false);
	resourceManager->DestroySampler(sampler, fence, false);
	resourceManager->DestroyTexture(texture, fence, false);
}

#endif  // BERNINI_GPU_DEBUG
