#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/GraphicsKernel.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include "types/Format.h"
#include "types/GraphicsState.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/Viewport.h>
#include <catch2/catch_approx.hpp>

// The D3D12 half of IGraphicsPipeline: the same FullscreenRect module MeshletRender_test draws
// through the mesh stage, drawn instead through VSMain over SV_VertexID. Every texel white proves
// the covering triangle came out of the vertex path with the right winding -- and, because both
// tests draw the same shader to the same target, that the two seams agree.
TEST_CASE("Graphics pipeline renders a fullscreen triangle", "[graphics]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
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
	texDesc.debugName     = "Graphics Render Target";
	texDesc.clearValue.SetColor(bgl::Color(0.0f, 0.0f, 0.0f, 1.0f));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc      = bgl::RtvDesc();
	rtvDesc.format    = bgl::Format::RGBA32_FLOAT;
	rtvDesc.debugName = "Graphics RTV";

	auto rtv = resourceManager->CreateRtv(tex, rtvDesc);

	auto kernel = device->CreateGraphicsKernel(
		bgl::GraphicsPipelineDesc()
			.SetVertexShader(device->CreateShader("FullscreenRect", "VSMain"))
			.SetPixelShader(device->CreateShader("FullscreenRect", "PSMain"))
			.AddRtvFormat(bgl::Format::RGBA32_FLOAT)
			.SetDebugName("fullscreen-vertex"));

	REQUIRE(kernel.pipeline.IsInitialized());

	auto state   = bgl::GraphicsState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgl::Viewport(static_cast<float>(width), static_cast<float>(height)));
	state.frameBuffer.AddColorAttachment(rtv);

	auto layout      = resourceManager->GetTextureReadbackLayout(tex);
	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Graphics Readback";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	resourceManager->ClearRtv(cmdList, rtv, clearColor);

	cmdList->SetGraphicsState(state);
	cmdList->Draw(3);
	cmdList->EndRenderPass();

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

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyRtv(rtv, false);
	resourceManager->DestroyTexture(tex, false);
}
