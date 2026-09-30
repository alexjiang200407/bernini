#include "gfx/GraphicsBase.h"
#include "passes/draw_bucket_config.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/IGraphics.h>
#include <bgl/types/Viewport.h>
#include <bgl_common/idl/DispatchArgs.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/MeshletState.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// The first mesh-shader render test: a mesh pipeline (FullscreenRect: MSMain emits one full-screen
// triangle, PSMain writes solid white) clears an offscreen RT to black, draws over it, reads it
// back, and checks every texel is white. Exercises the meshlet pipeline, the render encoder, and
TEST_CASE("Meshlet pipeline renders a fullscreen triangle", "[meshlet]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.gpuContext.enablePixDebug           = true;

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc = bgpu::CommandListDesc();
	cmdListDesc.type = bgpu::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	const uint32_t width  = 4;
	const uint32_t height = 4;

	auto texDesc          = bgpu::TextureDesc();
	texDesc.width         = width;
	texDesc.height        = height;
	texDesc.format        = bgpu::Format::RGBA32_FLOAT;
	texDesc.usage         = bgpu::TextureUsageFlag::kRenderTarget;
	texDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
	texDesc.debugName     = "Meshlet Render Target";
	texDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc      = bgpu::RtvDesc();
	rtvDesc.format    = bgpu::Format::RGBA32_FLOAT;
	rtvDesc.debugName = "Meshlet RTV";

	auto rtv = resourceManager->CreateRtv(tex, rtvDesc);

	auto kernel = device->CreateMeshletKernel(
		bgpu::MeshletPipelineDesc()
			.SetMeshShader(device->CreateShader("programs.screen.FullscreenRect", "MSMain"))
			.SetPixelShader(device->CreateShader("programs.screen.FullscreenRect", "PSMain"))
			.AddRtvFormat(bgpu::Format::RGBA32_FLOAT));

	auto state   = bgpu::MeshletState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgpu::Viewport(static_cast<float>(width), static_cast<float>(height)));
	state.frameBuffer.AddColorAttachment(rtv);

	auto layout      = resourceManager->GetTextureReadbackLayout(tex);
	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Meshlet Readback";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	resourceManager->ClearRtv(cmdList, rtv, clearColor);

	cmdList->SetMeshletState(state);
	cmdList->DispatchMesh(1, 1, 1);

	// Move the texture from render-target to copy-source for the readback.
	auto barrier = bgpu::TextureBarrierDesc();
	barrier.AddSyncBefore(bgpu::BarrierSyncFlag::kRenderTarget)
		.AddAccessBefore(bgpu::BarrierAccessFlag::kRenderTarget)
		.SetLayoutBefore(bgpu::BarrierLayout::kRenderTarget)
		.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
		.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
		.SetLayoutAfter(bgpu::BarrierLayout::kCopySource);
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

// The mesh shader reads a uniform and passes it to the fragment, which reads another -- so a wrong
// binding in either stage shows up as a wrong output channel. Verifies mesh-shader uniform binding.
TEST_CASE("Meshlet pipeline binds uniforms to the mesh and fragment stages", "[meshlet]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.gpuContext.enablePixDebug           = true;

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc = bgpu::CommandListDesc();
	cmdListDesc.type = bgpu::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	const uint32_t width  = 4;
	const uint32_t height = 4;

	auto texDesc          = bgpu::TextureDesc();
	texDesc.width         = width;
	texDesc.height        = height;
	texDesc.format        = bgpu::Format::RGBA32_FLOAT;
	texDesc.usage         = bgpu::TextureUsageFlag::kRenderTarget;
	texDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
	texDesc.debugName     = "Mesh Uniform Target";
	texDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc   = bgpu::RtvDesc();
	rtvDesc.format = bgpu::Format::RGBA32_FLOAT;

	auto rtv = resourceManager->CreateRtv(tex, rtvDesc);

	auto kernel = device->CreateMeshletKernel(
		bgpu::MeshletPipelineDesc()
			.SetMeshShader(device->CreateShader("MeshUniformTest", "MSMain"))
			.SetPixelShader(device->CreateShader("MeshUniformTest", "PSMain"))
			.AddRtvFormat(bgpu::Format::RGBA32_FLOAT));

	// R comes from the mesh stage (meshValue), G/B from the fragment stage (fragColor).
	kernel["gUniforms"]["meshValue"] = 0.25f;
	kernel["gUniforms"]["fragColor"] = glm::vec4(0.0f, 0.5f, 0.75f, 0.0f);

	auto state   = bgpu::MeshletState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgpu::Viewport(static_cast<float>(width), static_cast<float>(height)));
	state.frameBuffer.AddColorAttachment(rtv);

	auto layout      = resourceManager->GetTextureReadbackLayout(tex);
	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Mesh Uniform Readback";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	cmdList->SetMeshletState(state);
	cmdList->DispatchMesh(1, 1, 1);

	auto barrier = bgpu::TextureBarrierDesc();
	barrier.AddSyncBefore(bgpu::BarrierSyncFlag::kRenderTarget)
		.AddAccessBefore(bgpu::BarrierAccessFlag::kRenderTarget)
		.SetLayoutBefore(bgpu::BarrierLayout::kRenderTarget)
		.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
		.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
		.SetLayoutAfter(bgpu::BarrierLayout::kCopySource);
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

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyRtv(rtv, false);
	resourceManager->DestroyTexture(tex, false);
}

// Two draws to one target in one command list. The second must see the first's attachment contents
// and its own uniforms -- which is what a backend reusing an open render encoder across draws has to
// get right, and what reopening a pass per draw would hide. Backend-agnostic.
TEST_CASE("Two meshlet draws to one target share a pass and rebind their uniforms", "[meshlet]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto  resourceManager = gfxBase->GetResourceManagerCpy();
	auto* device          = gfxBase->GetDevice();

	auto cmdListDesc = bgpu::CommandListDesc();
	cmdListDesc.type = bgpu::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	const uint32_t width  = 4;
	const uint32_t height = 4;

	auto texDesc          = bgpu::TextureDesc();
	texDesc.width         = width;
	texDesc.height        = height;
	texDesc.format        = bgpu::Format::RGBA32_FLOAT;
	texDesc.usage         = bgpu::TextureUsageFlag::kRenderTarget;
	texDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
	texDesc.debugName     = "Two Draw Target";
	texDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc   = bgpu::RtvDesc();
	rtvDesc.format = bgpu::Format::RGBA32_FLOAT;
	auto rtv       = resourceManager->CreateRtv(tex, rtvDesc);

	auto kernel = device->CreateMeshletKernel(
		bgpu::MeshletPipelineDesc()
			.SetMeshShader(device->CreateShader("MeshUniformTest", "MSMain"))
			.SetPixelShader(device->CreateShader("MeshUniformTest", "PSMain"))
			.AddRtvFormat(bgpu::Format::RGBA32_FLOAT));

	auto state   = bgpu::MeshletState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgpu::Viewport(static_cast<float>(width), static_cast<float>(height)));
	state.frameBuffer.AddColorAttachment(rtv);

	auto layout      = resourceManager->GetTextureReadbackLayout(tex);
	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Two Draw Readback";
	auto rb          = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	// Both draws cover the target, so the second's colour is what must survive. A backend that
	// bound the first draw's uniforms and never rebound would leave the first's.
	kernel["gUniforms"]["meshValue"] = 0.25f;
	kernel["gUniforms"]["fragColor"] = glm::vec4(0.0f, 0.5f, 0.75f, 0.0f);
	cmdList->SetMeshletState(state);
	cmdList->DispatchMesh(1, 1, 1);

	kernel["gUniforms"]["meshValue"] = 1.0f;
	kernel["gUniforms"]["fragColor"] = glm::vec4(0.0f, 0.125f, 0.375f, 0.0f);
	cmdList->SetMeshletState(state);
	cmdList->DispatchMesh(1, 1, 1);

	auto barrier = bgpu::TextureBarrierDesc();
	barrier.AddSyncBefore(bgpu::BarrierSyncFlag::kRenderTarget)
		.AddAccessBefore(bgpu::BarrierAccessFlag::kRenderTarget)
		.SetLayoutBefore(bgpu::BarrierLayout::kRenderTarget)
		.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
		.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
		.SetLayoutAfter(bgpu::BarrierLayout::kCopySource);
	cmdList->Barrier(tex, barrier);

	cmdList->CopyTextureToReadback(rb, tex);
	cmdList->Close();

	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

	const auto* base = static_cast<const uint8_t*>(resourceManager->MapReadback(rb));
	REQUIRE(base != nullptr);

	for (uint32_t y = 0; y < height; ++y)
	{
		const auto* row =
			reinterpret_cast<const float*>(base + layout.offset + y * layout.rowPitch);

		for (uint32_t x = 0; x < width; ++x)
		{
			CHECK(row[x * 4 + 0] == Catch::Approx(1.0f));
			CHECK(row[x * 4 + 1] == Catch::Approx(0.125f));
			CHECK(row[x * 4 + 2] == Catch::Approx(0.375f));
			CHECK(row[x * 4 + 3] == Catch::Approx(1.0f));
		}
	}

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyRtv(rtv, false);
	resourceManager->DestroyTexture(tex, false);
}

// Two cbuffers with disjoint stage usage (gMesh: mesh only, gFrag: fragment only) confirm the
// binding-index invariant the pipeline relies on -- a cbuffer's reflected index equals the
// [[buffer(N)]] each per-stage MSL places it at. A mismatch would read the wrong cbuffer and
// corrupt a channel.
TEST_CASE("Meshlet pipeline binds disjoint per-stage cbuffers correctly", "[meshlet]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.gpuContext.enablePixDebug           = true;

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc = bgpu::CommandListDesc();
	cmdListDesc.type = bgpu::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	const uint32_t width  = 4;
	const uint32_t height = 4;

	auto texDesc          = bgpu::TextureDesc();
	texDesc.width         = width;
	texDesc.height        = height;
	texDesc.format        = bgpu::Format::RGBA32_FLOAT;
	texDesc.usage         = bgpu::TextureUsageFlag::kRenderTarget;
	texDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
	texDesc.debugName     = "Two Cbuffer Target";
	texDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc   = bgpu::RtvDesc();
	rtvDesc.format = bgpu::Format::RGBA32_FLOAT;

	auto rtv = resourceManager->CreateRtv(tex, rtvDesc);

	auto kernel = device->CreateMeshletKernel(
		bgpu::MeshletPipelineDesc()
			.SetMeshShader(device->CreateShader("MeshTwoCbufferTest", "MSMain"))
			.SetPixelShader(device->CreateShader("MeshTwoCbufferTest", "PSMain"))
			.AddRtvFormat(bgpu::Format::RGBA32_FLOAT));

	kernel["gMesh"]["meshValue"] = 0.25f;  // mesh-only cbuffer -> R
	kernel["gFrag"]["fragColor"] =
		glm::vec4(0.0f, 0.5f, 0.75f, 0.0f);  // fragment-only cbuffer -> G/B

	auto state   = bgpu::MeshletState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgpu::Viewport(static_cast<float>(width), static_cast<float>(height)));
	state.frameBuffer.AddColorAttachment(rtv);

	auto layout      = resourceManager->GetTextureReadbackLayout(tex);
	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Two Cbuffer Readback";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	cmdList->SetMeshletState(state);
	cmdList->DispatchMesh(1, 1, 1);

	auto barrier = bgpu::TextureBarrierDesc();
	barrier.AddSyncBefore(bgpu::BarrierSyncFlag::kRenderTarget)
		.AddAccessBefore(bgpu::BarrierAccessFlag::kRenderTarget)
		.SetLayoutBefore(bgpu::BarrierLayout::kRenderTarget)
		.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
		.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
		.SetLayoutAfter(bgpu::BarrierLayout::kCopySource);
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
			CHECK(row[x * 4 + 0] == Catch::Approx(0.25f));  // from gMesh (mesh stage)
			CHECK(row[x * 4 + 1] == Catch::Approx(0.5f));   // from gFrag (fragment stage)
			CHECK(row[x * 4 + 2] == Catch::Approx(0.75f));
			CHECK(row[x * 4 + 3] == Catch::Approx(1.0f));
		}
	}

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyRtv(rtv, false);
	resourceManager->DestroyTexture(tex, false);
}

// The count-gated indirect verb, both halves of its contract: a zero count paired with a zero
// grid draws nothing, and a count of one draws. The drawing dispatch reads element 1 of both
// buffers, so a wrong stride on either fails the white check. Backend-agnostic: Metal ignores
// the count and relies on the zero grid, which is exactly the contract's precondition.
TEST_CASE("A count-gated indirect dispatch draws only what the count admits", "[meshlet]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.gpuContext.enablePixDebug           = true;

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc = bgpu::CommandListDesc();
	cmdListDesc.type = bgpu::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	const uint32_t width  = 4;
	const uint32_t height = 4;

	auto texDesc          = bgpu::TextureDesc();
	texDesc.width         = width;
	texDesc.height        = height;
	texDesc.format        = bgpu::Format::RGBA32_FLOAT;
	texDesc.usage         = bgpu::TextureUsageFlag::kRenderTarget;
	texDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
	texDesc.debugName     = "Count Dispatch Target";
	texDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc   = bgpu::RtvDesc();
	rtvDesc.format = bgpu::Format::RGBA32_FLOAT;

	auto rtv = resourceManager->CreateRtv(tex, rtvDesc);

	auto kernel = device->CreateMeshletKernel(
		bgpu::MeshletPipelineDesc()
			.SetMeshShader(device->CreateShader("programs.screen.FullscreenRect", "MSMain"))
			.SetPixelShader(device->CreateShader("programs.screen.FullscreenRect", "PSMain"))
			.AddRtvFormat(bgpu::Format::RGBA32_FLOAT));

	auto argsDesc = bgpu::ComputeBufferDesc();
	argsDesc.SetElement<bgl::idl::DispatchArgs>().SetInitialCount(2).SetDebugName(
		"Count Dispatch Args");

	auto argsBuf = resourceManager->CreateComputeBuffer(argsDesc);

	auto countDesc = bgpu::ComputeBufferDesc();
	countDesc.SetElement<uint32_t>().SetInitialCount(2).SetDebugName("Count Dispatch Counts");

	auto countBuf = resourceManager->CreateComputeBuffer(countDesc);

	auto state   = bgpu::MeshletState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgpu::Viewport(static_cast<float>(width), static_cast<float>(height)));
	state.frameBuffer.AddColorAttachment(rtv);
	state.indirectArgs  = argsBuf;
	state.commandCounts = countBuf;

	auto layout      = resourceManager->GetTextureReadbackLayout(tex);
	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Count Dispatch Readback";

	auto rbEmpty = resourceManager->CreateReadbackBuffer(rbDesc);
	auto rbDrawn = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	// Element 0 is the empty row: zero grid, zero count. Element 1 draws.
	const std::array<bgl::idl::DispatchArgs, 2> args   = { { { 0u, 1u, 1u }, { 1u, 1u, 1u } } };
	const std::array<uint32_t, 2>               counts = { 0u, 1u };

	cmdList->WriteBuffer(argsBuf, args.data(), sizeof(args));
	cmdList->WriteBuffer(countBuf, counts.data(), sizeof(counts));

	auto toIndirect = bgpu::BufferBarrierDesc()
	                      .AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
	                      .AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
	                      .AddSyncAfter(bgpu::BarrierSyncFlag::kIndirectArgument)
	                      .AddAccessAfter(bgpu::BarrierAccessFlag::kIndirectArgument);
	cmdList->Barrier(argsBuf, toIndirect);
	cmdList->Barrier(countBuf, toIndirect);

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	resourceManager->ClearRtv(cmdList, rtv, clearColor);

	cmdList->SetMeshletState(state);
	cmdList->DispatchMeshIndirectCount(0, 0);

	auto toCopySrc = bgpu::TextureBarrierDesc();
	toCopySrc.AddSyncBefore(bgpu::BarrierSyncFlag::kRenderTarget)
		.AddAccessBefore(bgpu::BarrierAccessFlag::kRenderTarget)
		.SetLayoutBefore(bgpu::BarrierLayout::kRenderTarget)
		.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
		.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
		.SetLayoutAfter(bgpu::BarrierLayout::kCopySource);
	cmdList->Barrier(tex, toCopySrc);

	cmdList->CopyTextureToReadback(rbEmpty, tex);

	auto toRenderTarget = bgpu::TextureBarrierDesc();
	toRenderTarget.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
		.AddAccessBefore(bgpu::BarrierAccessFlag::kCopySource)
		.SetLayoutBefore(bgpu::BarrierLayout::kCopySource)
		.AddSyncAfter(bgpu::BarrierSyncFlag::kRenderTarget)
		.AddAccessAfter(bgpu::BarrierAccessFlag::kRenderTarget)
		.SetLayoutAfter(bgpu::BarrierLayout::kRenderTarget);
	cmdList->Barrier(tex, toRenderTarget);

	cmdList->SetMeshletState(state);
	cmdList->DispatchMeshIndirectCount(1, 1);

	cmdList->Barrier(tex, toCopySrc);
	cmdList->CopyTextureToReadback(rbDrawn, tex);
	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* emptyBase = static_cast<const uint8_t*>(resourceManager->MapReadback(rbEmpty));
	REQUIRE(emptyBase != nullptr);

	for (uint32_t y = 0; y < height; ++y)
	{
		const auto* row =
			reinterpret_cast<const float*>(emptyBase + layout.offset + y * layout.rowPitch);

		for (uint32_t x = 0; x < width; ++x)
		{
			CHECK(row[x * 4 + 0] == Catch::Approx(0.0f));
			CHECK(row[x * 4 + 1] == Catch::Approx(0.0f));
			CHECK(row[x * 4 + 2] == Catch::Approx(0.0f));
			CHECK(row[x * 4 + 3] == Catch::Approx(1.0f));
		}
	}

	resourceManager->UnmapReadback(rbEmpty);

	const auto* drawnBase = static_cast<const uint8_t*>(resourceManager->MapReadback(rbDrawn));
	REQUIRE(drawnBase != nullptr);

	for (uint32_t y = 0; y < height; ++y)
	{
		const auto* row =
			reinterpret_cast<const float*>(drawnBase + layout.offset + y * layout.rowPitch);

		for (uint32_t x = 0; x < width; ++x)
		{
			CHECK(row[x * 4 + 0] == Catch::Approx(1.0f));
			CHECK(row[x * 4 + 1] == Catch::Approx(1.0f));
			CHECK(row[x * 4 + 2] == Catch::Approx(1.0f));
			CHECK(row[x * 4 + 3] == Catch::Approx(1.0f));
		}
	}

	resourceManager->UnmapReadback(rbDrawn);

	resourceManager->DestroyReadbackBuffer(rbEmpty, false);
	resourceManager->DestroyReadbackBuffer(rbDrawn, false);
	resourceManager->DestroyBuffer(argsBuf, false);
	resourceManager->DestroyBuffer(countBuf, false);
	resourceManager->DestroyRtv(rtv, false);
	resourceManager->DestroyTexture(tex, false);
}

// The half of the contract only D3D12 keeps: a zero count suppresses the dispatch even when the
// grid is non-zero. This is the assertion that separates the count-gated verb from a plain
// DispatchMeshIndirect -- Metal never reads the count by documented design, so the case is
// D3D12-only.
#if defined(RENDERER_BACKEND_DX12)
TEST_CASE("A zero count suppresses a non-zero grid on D3D12", "[meshlet]")
{
	auto opts                                = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.gpuContext.enableDebugLayer         = true;
	opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.gpuContext.enablePixDebug           = true;

	auto gfx = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc = bgpu::CommandListDesc();
	cmdListDesc.type = bgpu::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	const uint32_t width  = 4;
	const uint32_t height = 4;

	auto texDesc          = bgpu::TextureDesc();
	texDesc.width         = width;
	texDesc.height        = height;
	texDesc.format        = bgpu::Format::RGBA32_FLOAT;
	texDesc.usage         = bgpu::TextureUsageFlag::kRenderTarget;
	texDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
	texDesc.debugName     = "Suppressed Dispatch Target";
	texDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc   = bgpu::RtvDesc();
	rtvDesc.format = bgpu::Format::RGBA32_FLOAT;

	auto rtv = resourceManager->CreateRtv(tex, rtvDesc);

	auto kernel = device->CreateMeshletKernel(
		bgpu::MeshletPipelineDesc()
			.SetMeshShader(device->CreateShader("programs.screen.FullscreenRect", "MSMain"))
			.SetPixelShader(device->CreateShader("programs.screen.FullscreenRect", "PSMain"))
			.AddRtvFormat(bgpu::Format::RGBA32_FLOAT));

	auto argsDesc = bgpu::ComputeBufferDesc();
	argsDesc.SetElement<bgl::idl::DispatchArgs>().SetInitialCount(1).SetDebugName(
		"Suppressed Dispatch Args");

	auto argsBuf = resourceManager->CreateComputeBuffer(argsDesc);

	auto countDesc = bgpu::ComputeBufferDesc();
	countDesc.SetElement<uint32_t>().SetInitialCount(1).SetDebugName("Suppressed Dispatch Count");

	auto countBuf = resourceManager->CreateComputeBuffer(countDesc);

	auto state   = bgpu::MeshletState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgpu::Viewport(static_cast<float>(width), static_cast<float>(height)));
	state.frameBuffer.AddColorAttachment(rtv);
	state.indirectArgs  = argsBuf;
	state.commandCounts = countBuf;

	auto layout      = resourceManager->GetTextureReadbackLayout(tex);
	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Suppressed Dispatch Readback";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	// A grid that would paint the target white, behind a count of zero.
	const bgl::idl::DispatchArgs args  = { 1u, 1u, 1u };
	const uint32_t               count = 0u;

	cmdList->WriteBuffer(argsBuf, &args, sizeof(args));
	cmdList->WriteBuffer(countBuf, &count, sizeof(count));

	auto toIndirect = bgpu::BufferBarrierDesc()
	                      .AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
	                      .AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
	                      .AddSyncAfter(bgpu::BarrierSyncFlag::kIndirectArgument)
	                      .AddAccessAfter(bgpu::BarrierAccessFlag::kIndirectArgument);
	cmdList->Barrier(argsBuf, toIndirect);
	cmdList->Barrier(countBuf, toIndirect);

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	resourceManager->ClearRtv(cmdList, rtv, clearColor);

	cmdList->SetMeshletState(state);
	cmdList->DispatchMeshIndirectCount(0, 0);

	auto toCopySrc = bgpu::TextureBarrierDesc();
	toCopySrc.AddSyncBefore(bgpu::BarrierSyncFlag::kRenderTarget)
		.AddAccessBefore(bgpu::BarrierAccessFlag::kRenderTarget)
		.SetLayoutBefore(bgpu::BarrierLayout::kRenderTarget)
		.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
		.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
		.SetLayoutAfter(bgpu::BarrierLayout::kCopySource);
	cmdList->Barrier(tex, toCopySrc);

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
			CHECK(row[x * 4 + 0] == Catch::Approx(0.0f));
			CHECK(row[x * 4 + 1] == Catch::Approx(0.0f));
			CHECK(row[x * 4 + 2] == Catch::Approx(0.0f));
			CHECK(row[x * 4 + 3] == Catch::Approx(1.0f));
		}
	}

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyBuffer(argsBuf, false);
	resourceManager->DestroyBuffer(countBuf, false);
	resourceManager->DestroyRtv(rtv, false);
	resourceManager->DestroyTexture(tex, false);
}

// The shape the geometry passes dispatch in: one buffer as both the argument and the count buffer,
// the count read off each entry's threadCountX (DrawBucketCountIndex). A grid of 5 is a count of 5,
// which the verb clamps to one command -- it draws, once, reading nothing past its own entry -- and
// a grid of 0 is a count of 0, which draws nothing.
TEST_CASE("Dispatch args serve as their own count buffer on D3D12", "[meshlet]")
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

	auto cmdListDesc = bgpu::CommandListDesc();
	cmdListDesc.type = bgpu::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	const uint32_t width  = 4;
	const uint32_t height = 4;

	auto texDesc          = bgpu::TextureDesc();
	texDesc.width         = width;
	texDesc.height        = height;
	texDesc.format        = bgpu::Format::RGBA32_FLOAT;
	texDesc.usage         = bgpu::TextureUsageFlag::kRenderTarget;
	texDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
	texDesc.debugName     = "Self-Counted Dispatch Target";
	texDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

	auto tex = resourceManager->CreateTexture(texDesc);

	auto rtvDesc   = bgpu::RtvDesc();
	rtvDesc.format = bgpu::Format::RGBA32_FLOAT;

	auto rtv = resourceManager->CreateRtv(tex, rtvDesc);

	auto kernel = device->CreateMeshletKernel(
		bgpu::MeshletPipelineDesc()
			.SetMeshShader(device->CreateShader("programs.screen.FullscreenRect", "MSMain"))
			.SetPixelShader(device->CreateShader("programs.screen.FullscreenRect", "PSMain"))
			.AddRtvFormat(bgpu::Format::RGBA32_FLOAT));

	// Two draw buckets' entries: a filled one, and an empty one after it.
	auto argsDesc = bgpu::ComputeBufferDesc();
	argsDesc.SetElement<bgl::idl::DispatchArgs>().SetInitialCount(2).SetDebugName(
		"Self-Counted Dispatch Args");

	auto argsBuf = resourceManager->CreateComputeBuffer(argsDesc);

	auto state   = bgpu::MeshletState();
	state.kernel = &kernel;
	state.viewportState.AddViewportAndScissorRect(
		bgpu::Viewport(static_cast<float>(width), static_cast<float>(height)));
	state.frameBuffer.AddColorAttachment(rtv);
	state.indirectArgs  = argsBuf;
	state.commandCounts = argsBuf;

	auto layout      = resourceManager->GetTextureReadbackLayout(tex);
	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "Self-Counted Dispatch Readback";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	const auto drawBucket = [&](const uint32_t bucket) {
		cmdList->Open(cmdQueue, cmdAllocator);

		const std::array<bgl::idl::DispatchArgs, 2> args = { {
			{ 5u, 1u, 1u },
			{ 0u, 1u, 1u },
		} };
		cmdList->WriteBuffer(argsBuf, args.data(), sizeof(args));
		cmdList->Barrier(
			argsBuf,
			bgpu::BufferBarrierDesc()
				.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
				.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
				.AddSyncAfter(bgpu::BarrierSyncFlag::kIndirectArgument)
				.AddAccessAfter(bgpu::BarrierAccessFlag::kIndirectArgument));

		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		resourceManager->ClearRtv(cmdList, rtv, clearColor);

		cmdList->SetMeshletState(state);
		cmdList->DispatchMeshIndirectCount(bucket, bgl::DrawBucketCountIndex(bucket));

		auto toCopySrc = bgpu::TextureBarrierDesc();
		toCopySrc.AddSyncBefore(bgpu::BarrierSyncFlag::kRenderTarget)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kRenderTarget)
			.SetLayoutBefore(bgpu::BarrierLayout::kRenderTarget)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
			.SetLayoutAfter(bgpu::BarrierLayout::kCopySource);
		cmdList->Barrier(tex, toCopySrc);

		cmdList->CopyTextureToReadback(rb, tex);

		auto toTarget = bgpu::TextureBarrierDesc();
		toTarget.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kCopySource)
			.SetLayoutBefore(bgpu::BarrierLayout::kCopySource)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kRenderTarget)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kRenderTarget)
			.SetLayoutAfter(bgpu::BarrierLayout::kRenderTarget);
		cmdList->Barrier(tex, toTarget);
		cmdList->Close();

		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

		const auto* base = static_cast<const uint8_t*>(resourceManager->MapReadback(rb));
		REQUIRE(base != nullptr);
		const float red = reinterpret_cast<const float*>(base + layout.offset)[0];
		resourceManager->UnmapReadback(rb);
		return red;
	};

	CHECK(drawBucket(0) == Catch::Approx(1.0f));
	CHECK(drawBucket(1) == Catch::Approx(0.0f));

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyBuffer(argsBuf, false);
	resourceManager->DestroyRtv(rtv, false);
	resourceManager->DestroyTexture(tex, false);
}
#endif
