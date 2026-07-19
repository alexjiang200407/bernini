#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/MeshletKernel.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include "types/Format.h"
#include "types/MeshletState.h"
#include "util/GpuValidation.h"
#include <bgl/IGraphics.h>
#include <bgl/Viewport.h>
#include <catch2/catch_approx.hpp>

// The first mesh-shader render test: a mesh pipeline (FullscreenRect: MSMain emits one full-screen
// triangle, PSMain writes solid white) clears an offscreen RT to black, draws over it, reads it
// back, and checks every texel is white. Exercises the meshlet pipeline, the render encoder, and
// drawMeshThreadgroups. Backend-agnostic; runs on D3D12 and (tagged [metal]) on Metal.
TEST_CASE("Meshlet pipeline renders a fullscreen triangle", "[meshlet][metal]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.enablePixDebug           = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc = bgl::CommandListDesc();
	cmdListDesc.type = bgl::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	const uint32_t width  = 4;
	const uint32_t height = 4;

	auto texDesc          = bgl::TextureDesc();
	texDesc.width         = width;
	texDesc.height        = height;
	texDesc.format        = bgl::Format::RGBA32_FLOAT;
	texDesc.usage         = bgl::TextureUsageFlag::kRenderTarget;
	texDesc.initialLayout = bgl::BarrierLayout::kRenderTarget;
	texDesc.debugName     = "Meshlet Render Target";
	texDesc.clearValue.SetColor(bgl::Color(0.0f, 0.0f, 0.0f, 1.0f));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc      = bgl::RtvDesc();
	rtvDesc.format    = bgl::Format::RGBA32_FLOAT;
	rtvDesc.debugName = "Meshlet RTV";

	auto rtv = resourceManager->CreateRtv(tex, rtvDesc);

	auto kernel = device->CreateMeshletKernel(
		bgl::MeshletPipelineDesc()
			.SetMeshShader(device->CreateShader("FullscreenRect", "MSMain"))
			.SetPixelShader(device->CreateShader("FullscreenRect", "PSMain"))
			.AddRtvFormat(bgl::Format::RGBA32_FLOAT));

	auto state   = bgl::MeshletState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgl::Viewport(static_cast<float>(width), static_cast<float>(height)));
	state.frameBuffer.AddColorAttachment(rtv);

	auto layout      = resourceManager->GetTextureReadbackLayout(tex);
	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Meshlet Readback";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	resourceManager->ClearRtv(cmdList, rtv, clearColor);

	cmdList->SetMeshletState(state);
	cmdList->DispatchMesh(1, 1, 1);

	// Move the texture from render-target to copy-source for the readback.
	auto barrier = bgl::TextureBarrierDesc();
	barrier.AddSyncBefore(bgl::BarrierSyncFlag::kRenderTarget)
		.AddAccessBefore(bgl::BarrierAccessFlag::kRenderTarget)
		.SetLayoutBefore(bgl::BarrierLayout::kRenderTarget)
		.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
		.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource)
		.SetLayoutAfter(bgl::BarrierLayout::kCopySource);
	cmdList->Barrier(tex, barrier);

	cmdList->CopyTextureToReadback(rb, tex);
	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* base = static_cast<const uint8_t*>(resourceManager->MapReadback(rb));
	REQUIRE(base != nullptr);

	// The full-screen triangle covers every texel, so all should be solid white.
	for (uint32_t y = 0; y < height; ++y)
	{
		const auto* row =
			reinterpret_cast<const float*>(base + layout.offset + y * layout.rowPitch);

		for (uint32_t x = 0; x < width; ++x)
		{
			CHECK(row[x * 4 + 0] == Catch::Approx(1.0f));
			CHECK(row[x * 4 + 1] == Catch::Approx(1.0f));
			CHECK(row[x * 4 + 2] == Catch::Approx(1.0f));
			CHECK(row[x * 4 + 3] == Catch::Approx(1.0f));
		}
	}

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, fence, false);
	resourceManager->DestroyRtv(rtv, fence, false);
	resourceManager->DestroyTexture(tex, fence, false);
}

// The mesh shader reads a uniform and passes it to the fragment, which reads another -- so a wrong
// binding in either stage shows up as a wrong output channel. Verifies mesh-shader uniform binding.
TEST_CASE("Meshlet pipeline binds uniforms to the mesh and fragment stages", "[meshlet][metal]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.enablePixDebug           = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc = bgl::CommandListDesc();
	cmdListDesc.type = bgl::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	const uint32_t width  = 4;
	const uint32_t height = 4;

	auto texDesc          = bgl::TextureDesc();
	texDesc.width         = width;
	texDesc.height        = height;
	texDesc.format        = bgl::Format::RGBA32_FLOAT;
	texDesc.usage         = bgl::TextureUsageFlag::kRenderTarget;
	texDesc.initialLayout = bgl::BarrierLayout::kRenderTarget;
	texDesc.debugName     = "Mesh Uniform Target";
	texDesc.clearValue.SetColor(bgl::Color(0.0f, 0.0f, 0.0f, 1.0f));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc   = bgl::RtvDesc();
	rtvDesc.format = bgl::Format::RGBA32_FLOAT;

	auto rtv = resourceManager->CreateRtv(tex, rtvDesc);

	auto kernel = device->CreateMeshletKernel(
		bgl::MeshletPipelineDesc()
			.SetMeshShader(device->CreateShader("MeshUniformTest", "MSMain"))
			.SetPixelShader(device->CreateShader("MeshUniformTest", "PSMain"))
			.AddRtvFormat(bgl::Format::RGBA32_FLOAT));

	// R comes from the mesh stage (meshValue), G/B from the fragment stage (fragColor).
	kernel["gUniforms"]["meshValue"] = 0.25f;
	kernel["gUniforms"]["fragColor"] = glm::vec4(0.0f, 0.5f, 0.75f, 0.0f);

	auto state   = bgl::MeshletState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgl::Viewport(static_cast<float>(width), static_cast<float>(height)));
	state.frameBuffer.AddColorAttachment(rtv);

	auto layout      = resourceManager->GetTextureReadbackLayout(tex);
	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Mesh Uniform Readback";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	cmdList->SetMeshletState(state);
	cmdList->DispatchMesh(1, 1, 1);

	auto barrier = bgl::TextureBarrierDesc();
	barrier.AddSyncBefore(bgl::BarrierSyncFlag::kRenderTarget)
		.AddAccessBefore(bgl::BarrierAccessFlag::kRenderTarget)
		.SetLayoutBefore(bgl::BarrierLayout::kRenderTarget)
		.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
		.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource)
		.SetLayoutAfter(bgl::BarrierLayout::kCopySource);
	cmdList->Barrier(tex, barrier);

	cmdList->CopyTextureToReadback(rb, tex);
	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* base = static_cast<const uint8_t*>(resourceManager->MapReadback(rb));
	REQUIRE(base != nullptr);

	for (uint32_t y = 0; y < height; ++y)
	{
		const auto* row =
			reinterpret_cast<const float*>(base + layout.offset + y * layout.rowPitch);

		for (uint32_t x = 0; x < width; ++x)
		{
			CHECK(row[x * 4 + 0] == Catch::Approx(0.25f));  // mesh-stage uniform
			CHECK(row[x * 4 + 1] == Catch::Approx(0.5f));   // fragment-stage uniform
			CHECK(row[x * 4 + 2] == Catch::Approx(0.75f));
			CHECK(row[x * 4 + 3] == Catch::Approx(1.0f));
		}
	}

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, fence, false);
	resourceManager->DestroyRtv(rtv, fence, false);
	resourceManager->DestroyTexture(tex, fence, false);
}
