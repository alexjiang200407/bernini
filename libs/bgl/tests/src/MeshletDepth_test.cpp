#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/MeshletKernel.h"
#include "resource/Dsv.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include "types/Barrier.h"
#include "types/DepthStencilState.h"
#include "types/Format.h"
#include "types/MeshletState.h"
#include "util/GpuValidation.h"
#include <bgl/IGraphics.h>
#include <bgl/Viewport.h>
#include <catch2/catch_approx.hpp>

// Two solid-color fullscreen triangles at different depths, drawn near-first then far. With a Less
// depth test the far draw must be rejected, so the near colour survives -- proving the depth
// attachment, the MTLDepthStencilState (compare + write), and ClearDsv all take effect. If the test
// or write were broken the far (green) draw would overwrite the near (red). Backend-agnostic; runs
// on D3D12 and (tagged [metal]) on Metal. Uses D32 rather than the engine's D24S8, which Apple
// silicon does not support -- the Scene's format remap is a later render-loop concern.
TEST_CASE("Meshlet pipeline depth-tests two overlapping draws", "[meshlet][depth][metal]")
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
	texDesc.debugName     = "Depth Test Color Target";
	texDesc.clearValue.SetColor(bgl::Color(0.0f, 0.0f, 0.0f, 1.0f));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc   = bgl::RtvDesc();
	rtvDesc.format = bgl::Format::RGBA32_FLOAT;

	auto rtv = resourceManager->CreateRtv(tex, rtvDesc);

	auto depthDesc          = bgl::TextureDesc();
	depthDesc.width         = width;
	depthDesc.height        = height;
	depthDesc.format        = bgl::Format::D32;
	depthDesc.usage         = bgl::TextureUsageFlag::kDepthStencil;
	depthDesc.initialLayout = bgl::BarrierLayout::kDepthWrite;
	depthDesc.debugName     = "Depth Test Depth Buffer";
	depthDesc.clearValue.SetDepthStencil(1.0f, 0);

	auto depthTex = resourceManager->CreateTexture(depthDesc);

	auto dsvDesc   = bgl::DsvDesc();
	dsvDesc.format = bgl::Format::D32;

	auto dsv = resourceManager->CreateDsv(depthTex, dsvDesc);
	REQUIRE(resourceManager->ValidDsvHandle(dsv));

	auto pipelineDesc = bgl::MeshletPipelineDesc()
	                        .SetMeshShader(device->CreateShader("MeshDepthTest", "MSMain"))
	                        .SetPixelShader(device->CreateShader("MeshDepthTest", "PSMain"))
	                        .AddRtvFormat(bgl::Format::RGBA32_FLOAT)
	                        .SetDsvFormat(bgl::Format::D32);
	pipelineDesc.renderState.depthStencilState.EnableDepthTest().EnableDepthWrite().SetDepthFunc(
		bgl::ComparisonFunc::kLess);

	auto kernel = device->CreateMeshletKernel(pipelineDesc);

	auto state   = bgl::MeshletState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgl::Viewport(static_cast<float>(width), static_cast<float>(height)));
	state.frameBuffer.AddColorAttachment(rtv).SetDepthAttachment(dsv);

	auto layout      = resourceManager->GetTextureReadbackLayout(tex);
	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Depth Test Readback";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	resourceManager->ClearRtv(cmdList, rtv, clearColor);
	resourceManager->ClearDsv(cmdList, dsv, 1.0f, 0);

	cmdList->SetMeshletState(state);

	// Near, red -- passes the cleared depth (0.3 < 1.0) and writes 0.3.
	kernel["gUniforms"]["depth"] = 0.3f;
	kernel["gUniforms"]["color"] = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	cmdList->DispatchMesh(1, 1, 1);

	// Far, green -- fails the test (0.7 < 0.3 is false) and must not touch the colour target.
	kernel["gUniforms"]["depth"] = 0.7f;
	kernel["gUniforms"]["color"] = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
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

	// The near red draw occludes the far green one: every texel is red.
	for (uint32_t y = 0; y < height; ++y)
	{
		const auto* row =
			reinterpret_cast<const float*>(base + layout.offset + y * layout.rowPitch);

		for (uint32_t x = 0; x < width; ++x)
		{
			CHECK(row[x * 4 + 0] == Catch::Approx(1.0f));
			CHECK(row[x * 4 + 1] == Catch::Approx(0.0f));
			CHECK(row[x * 4 + 2] == Catch::Approx(0.0f));
			CHECK(row[x * 4 + 3] == Catch::Approx(1.0f));
		}
	}

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, fence, false);
	resourceManager->DestroyDsv(dsv, fence, false);
	resourceManager->DestroyTexture(depthTex, fence, false);
	resourceManager->DestroyRtv(rtv, fence, false);
	resourceManager->DestroyTexture(tex, fence, false);
}
