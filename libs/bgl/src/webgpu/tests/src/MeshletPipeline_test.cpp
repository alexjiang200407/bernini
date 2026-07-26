#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_wgpu.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "pipeline/MeshletPipeline_wgpu.h"
#include "resource/ResourceManager.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The WebGPU IMeshletPipeline: CreateMeshletKernel composes a GraphicsPipeline out of the mesh
// module's vertex + pixel entries, so the kernel/uniforms machinery works unchanged. Proven by
// drawing the composed pipeline through a command-list render pass -- the path DispatchMesh will
// drive once the meshlet-expansion kernel lands. RasterTriangleTest has no constant buffers, so the
// kernel carries no uniforms here.
TEST_CASE("A meshlet kernel composes a drawable graphics pipeline", "[wgpu][render]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	const auto shader = device->CreateShader(ShaderDesc{}.SetSlangModuleName("RasterTriangleTest"));

	auto desc = MeshletPipelineDesc{};
	desc.SetMeshShader(shader).SetPixelShader(shader).AddRtvFormat(Format::RGBA8_UNORM);
	desc.renderState.rasterState.SetCullNone();

	auto kernel = device->CreateMeshletKernel(desc);
	REQUIRE(kernel.pipeline != nullptr);
	REQUIRE(kernel.pipeline->GetUniformBufferNames().empty());

	const GraphicsPipeline& pipeline =
		static_cast<MeshletPipeline*>(kernel.pipeline.Get())->GetGraphicsPipeline();

	auto texDesc      = TextureDesc{};
	texDesc.width     = c_Size;
	texDesc.height    = c_Size;
	texDesc.format    = Format::RGBA8_UNORM;
	texDesc.usage     = TextureUsage(TextureUsageFlag::kRenderTarget);
	texDesc.debugName = "rt";

	const auto texture = resources->CreateTexture(texDesc);
	const auto rtv     = resources->CreateRtv(texture, RtvDesc{ .format = Format::RGBA8_UNORM });

	const auto fb = FrameBuffer().AddColorAttachment(rtv);

	auto viewport = ViewportState();
	viewport.AddViewportAndScissorRect(
		Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size)));

	const auto layout   = resources->GetTextureReadbackLayout(texture);
	const auto readback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = layout.totalBytes, .debugName = "rt-readback" });

	float clear[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

	auto* wgpuList = static_cast<CommandList*>(list.Get());

	list->Open(queue.Get(), allocator.Get());
	resources->ClearRtv(list.Get(), rtv, clear);
	wgpuList->BeginRenderPass(fb, viewport);
	wgpuList->SetGraphicsPipeline(pipeline);
	wgpuList->Draw(3);
	wgpuList->EndRenderPass();
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
