#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "resource/ResourceManager.h"
#include "types/GraphicsState.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The traditional vertex->pixel seam reached entirely through the RHI: IDevice::CreateGraphicsKernel
// builds the pipeline and its uniforms, ICommandList::SetGraphicsState opens the pass and binds
// them, and Draw issues the vertices. No backend type appears below the device handle, which is what
// distinguishes this from GraphicsPipeline_test -- that one constructs the WebGPU object directly.
//
// This is the seam the skybox and fullscreen passes move onto, so that they stop being mesh shaders
// on both backends.
TEST_CASE("A graphics kernel draws through the RHI", "[wgpu][render]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	auto desc = GraphicsPipelineDesc{};
	desc.SetVertexShader(
			device->CreateShader(ShaderDesc{}.SetSlangModuleName("RasterTriangleTest")))
		.SetPixelShader(device->CreateShader(ShaderDesc{}.SetSlangModuleName("MultiModulePixel")))
		.AddRtvFormat(Format::RGBA8_UNORM)
		.SetDebugName("rhi-graphics-kernel");
	desc.vertexEntry = "VSMain";
	desc.pixelEntry  = "PSMain";
	desc.renderState.rasterState.SetCullNone();

	auto kernel = device->CreateGraphicsKernel(desc);
	REQUIRE(kernel.pipeline.IsInitialized());

	auto texDesc      = TextureDesc{};
	texDesc.width     = c_Size;
	texDesc.height    = c_Size;
	texDesc.format    = Format::RGBA8_UNORM;
	texDesc.usage     = TextureUsage(TextureUsageFlag::kRenderTarget);
	texDesc.debugName = "rt";

	const auto texture = resources->CreateTexture(texDesc);
	const auto rtv     = resources->CreateRtv(texture, RtvDesc{ .format = Format::RGBA8_UNORM });

	auto state        = GraphicsState{};
	state.kernel      = &kernel;
	state.frameBuffer = FrameBuffer().AddColorAttachment(rtv);
	state.viewportState.AddViewportAndScissorRect(
		Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size)));

	const auto rbLayout = resources->GetTextureReadbackLayout(texture);
	const auto readback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = rbLayout.totalBytes, .debugName = "rt-readback" });

	float clear[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

	list->Open(queue.Get(), allocator.Get());
	resources->ClearRtv(list.Get(), rtv, clear);
	list->SetGraphicsState(state);
	list->Draw(3);
	list->EndRenderPass();
	list->CopyTextureToReadback(readback, texture);
	list->Close();

	const auto fence = queue->ExecuteCommandList(list.Get());
	queue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(resources->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	const auto at = [&](uint32_t x, uint32_t y) { return mapped[(y * c_Size) + x]; };
	CHECK(at(c_Size / 2, c_Size / 2) == 0xFF00FF00u);
	CHECK(at(0, 0) == 0xFF0000FFu);

	resources->UnmapReadback(readback);
	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}
