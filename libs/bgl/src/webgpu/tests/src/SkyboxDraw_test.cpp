#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "resource/ResourceManager.h"
#include "types/GraphicsState.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The production Skybox module end to end on WGSL: cube texture and sampler declared through the
// idl.TextureHandle / SamplerHandle BGL_WGSL forms, reflected into bindings, and drawn through the
// RHI. This is the shader SkyboxPass builds at CreateGraphics, so it compiling and sampling here is
// what stands between the WebGPU backend and pass init.
//
// clipToWorld is chosen so every pixel's view direction is +X dominant, because WriteTexture uploads
// only array layer 0 (the +X face) until W4. One face, one uniform colour, so the whole frame must
// come out one colour -- any per-pixel variation means the wrong face or a broken interpolant.
TEST_CASE("The skybox shader samples its cube through the RHI", "[wgpu][render][skybox]")
{
	constexpr uint32_t c_Size     = 64;
	constexpr uint32_t c_CubeEdge = 4;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	auto desc = GraphicsPipelineDesc{};
	desc.SetVertexShader(device->CreateShader("Skybox", "VSMain"))
		.SetPixelShader(device->CreateShader("Skybox", "PSMain"))
		.AddRtvFormat(Format::RGBA8_UNORM)
		.AddRtvFormat(Format::RG16_FLOAT)
		.SetDebugName("skybox-wgsl");
	desc.renderState.rasterState.SetCullNone();

	auto kernel = device->CreateGraphicsKernel(desc);
	REQUIRE(kernel.pipeline.IsInitialized());

	auto cubeDesc      = TextureDesc{};
	cubeDesc.width     = c_CubeEdge;
	cubeDesc.height    = c_CubeEdge;
	cubeDesc.arraySize = 6;
	cubeDesc.format    = Format::RGBA8_UNORM;
	cubeDesc.dimension = TextureDimension::kTextureCube;
	cubeDesc.usage     = TextureUsageFlag::kSRV;
	cubeDesc.debugName = "sky-cube";

	const auto cube = resources->CreateTexture(cubeDesc);
	REQUIRE(resources->IsTextureCube(cube));

	// Mid-grey: AgX carries a neutral through as a neutral, so the tonemapped output stays r=g=b.
	const auto texels = std::vector<uint32_t>(c_CubeEdge * c_CubeEdge, 0xFF808080u);

	auto sub       = TextureSubresourceData{};
	sub.data       = texels.data();
	sub.rowPitch   = c_CubeEdge * 4;
	sub.slicePitch = sub.rowPitch * c_CubeEdge;

	const auto sampler = resources->CreateSampler(
		SamplerDesc().SetAllFilters(true).SetAllAddressModes(SamplerAddressMode::kClamp));

	// world = (1, y/4, x/4): +X dominant at every ndc, so only the uploaded +X face is sampled.
	auto clipToWorld  = glm::mat4(0.0f);
	clipToWorld[2][0] = 1.0f;
	clipToWorld[1][1] = 0.25f;
	clipToWorld[0][2] = 0.25f;
	clipToWorld[3][3] = 1.0f;

	// prevClip.w = dir.x (never zero here), so the motion-vector divide stays finite.
	auto prevWorldToClip  = glm::mat4(1.0f);
	prevWorldToClip[0][3] = 1.0f;

	auto& skybox              = kernel["gSkyboxData"];
	skybox["clipToWorld"]     = clipToWorld;
	skybox["prevWorldToClip"] = prevWorldToClip;
	skybox["cubeTex"]         = cube;
	skybox["sampler"]         = sampler;
	skybox["exposure"]        = 1.0f;
	skybox["mipLevel"]        = 0.0f;

	auto colorDesc      = TextureDesc{};
	colorDesc.width     = c_Size;
	colorDesc.height    = c_Size;
	colorDesc.format    = Format::RGBA8_UNORM;
	colorDesc.usage     = TextureUsage(TextureUsageFlag::kRenderTarget);
	colorDesc.debugName = "sky-color";

	auto motionDesc      = colorDesc;
	motionDesc.format    = Format::RG16_FLOAT;
	motionDesc.debugName = "sky-motion";

	const auto color  = resources->CreateTexture(colorDesc);
	const auto motion = resources->CreateTexture(motionDesc);

	const auto colorRtv  = resources->CreateRtv(color, RtvDesc{ .format = Format::RGBA8_UNORM });
	const auto motionRtv = resources->CreateRtv(motion, RtvDesc{ .format = Format::RG16_FLOAT });

	auto state        = GraphicsState{};
	state.kernel      = &kernel;
	state.frameBuffer = FrameBuffer().AddColorAttachment(colorRtv).AddColorAttachment(motionRtv);
	state.viewportState.AddViewportAndScissorRect(
		Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size)));

	const auto rbLayout = resources->GetTextureReadbackLayout(color);
	const auto readback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = rbLayout.totalBytes, .debugName = "sky-readback" });

	float clear[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
	float zero[4]  = {};

	list->Open(queue.Get(), allocator.Get());
	list->WriteTexture(cube, std::span<const TextureSubresourceData>(&sub, 1));
	resources->ClearRtv(list.Get(), colorRtv, clear);
	resources->ClearRtv(list.Get(), motionRtv, zero);
	list->SetGraphicsState(state);
	list->Draw(3);
	list->EndRenderPass();
	list->CopyTextureToReadback(readback, color);
	list->Close();

	const auto fence = queue->ExecuteCommandList(list.Get());
	queue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(resources->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	const auto at = [&](uint32_t x, uint32_t y) { return mapped[(y * c_Size) + x]; };

	// One face, one colour: the sky must be uniform, covering, and not the clear.
	const uint32_t sky = at(c_Size / 2, c_Size / 2);
	CHECK(sky != 0xFF0000FFu);
	CHECK(at(0, 0) == sky);
	CHECK(at(c_Size - 1, 0) == sky);
	CHECK(at(0, c_Size - 1) == sky);
	CHECK(at(c_Size - 1, c_Size - 1) == sky);

	// A neutral in stays a neutral out, so a channel swap or a wrong face shows as a colour cast.
	const auto channel = [&](uint32_t shift) { return (sky >> shift) & 0xFF; };
	CHECK(channel(24) == 0xFF);
	CHECK(std::abs(static_cast<int>(channel(0)) - static_cast<int>(channel(8))) <= 1);
	CHECK(std::abs(static_cast<int>(channel(8)) - static_cast<int>(channel(16))) <= 1);
	CHECK(channel(0) > 0);

	resources->UnmapReadback(readback);
	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}
