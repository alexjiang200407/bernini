#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_wgpu.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "pipeline/GraphicsPipeline_wgpu.h"
#include "resource/ResourceManager.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The raster path through the RHI command list: a GraphicsPipeline drawn inside a
// CommandList::BeginRenderPass over a FrameBuffer -- the RTV that ResourceManager built -- rather
// than the raw wgpu render pass GraphicsPipeline_test drives. The target is cleared red, then the
// triangle is drawn with LoadOp::Load, so the readback proves two things at once: the draw ran (the
// centre is green) and the render pass loaded rather than re-cleared (the corners stay red).
TEST_CASE("A graphics pipeline draws through a command-list render pass", "[wgpu][render]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	auto texDesc      = TextureDesc{};
	texDesc.width     = c_Size;
	texDesc.height    = c_Size;
	texDesc.format    = Format::RGBA8_UNORM;
	texDesc.usage     = TextureUsage(TextureUsageFlag::kRenderTarget);
	texDesc.debugName = "rt";

	const auto texture = resources->CreateTexture(texDesc);
	const auto rtv     = resources->CreateRtv(texture, RtvDesc{ .format = Format::RGBA8_UNORM });

	auto desc         = GraphicsPipelineDesc{};
	desc.vertexShader = desc.pixelShader =
		device->CreateShader(ShaderDesc{}.SetSlangModuleName("RasterTriangleTest"));
	desc.vertexEntry = "VSMain";
	desc.pixelEntry  = "PSMain";
	desc.rtvFormats.push_back(Format::RGBA8_UNORM);
	desc.renderState.rasterState.SetCullNone();

	auto pipeline = GraphicsPipeline(device->GetHandle(), device->GetSlangSession(), desc);

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

	// RGBA8 little-endian: opaque green is 0xFF00FF00, opaque red is 0xFF0000FF.
	const auto at = [&](uint32_t x, uint32_t y) { return mapped[(y * c_Size) + x]; };
	CHECK(at(c_Size / 2, c_Size / 2) == 0xFF00FF00u);
	CHECK(at(0, 0) == 0xFF0000FFu);
	CHECK(at(c_Size - 1, c_Size - 1) == 0xFF0000FFu);

	resources->UnmapReadback(readback);
	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}

// The indirect draw the vertex-pulling forward path uses: the {vertexCount, instanceCount,
// firstVertex, firstInstance} arguments come from a buffer -- here written by the CPU, but the same
// buffer a meshlet-expansion kernel will fill on the GPU -- rather than from immediate values. A
// green triangle over the readback proves drawIndirect read the args and the buffer carried the
// Indirect usage.
TEST_CASE("A graphics pipeline draws indirectly from an args buffer", "[wgpu][render]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	auto texDesc      = TextureDesc{};
	texDesc.width     = c_Size;
	texDesc.height    = c_Size;
	texDesc.format    = Format::RGBA8_UNORM;
	texDesc.usage     = TextureUsage(TextureUsageFlag::kRenderTarget);
	texDesc.debugName = "rt";

	const auto texture = resources->CreateTexture(texDesc);
	const auto rtv     = resources->CreateRtv(texture, RtvDesc{ .format = Format::RGBA8_UNORM });

	auto desc         = GraphicsPipelineDesc{};
	desc.vertexShader = desc.pixelShader =
		device->CreateShader(ShaderDesc{}.SetSlangModuleName("RasterTriangleTest"));
	desc.vertexEntry = "VSMain";
	desc.pixelEntry  = "PSMain";
	desc.rtvFormats.push_back(Format::RGBA8_UNORM);
	desc.renderState.rasterState.SetCullNone();

	auto pipeline = GraphicsPipeline(device->GetHandle(), device->GetSlangSession(), desc);

	const auto args = resources->CreateComputeBuffer(
		ComputeBufferDesc{}.SetElement<uint32_t>().SetInitialCount(4).SetDebugName("draw-args"));

	// A non-indexed indirect draw: vertexCount, instanceCount, firstVertex, firstInstance.
	const uint32_t drawArgs[4] = { 3u, 1u, 0u, 0u };

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
	list->WriteBuffer(args, drawArgs, 0, sizeof(drawArgs));
	resources->ClearRtv(list.Get(), rtv, clear);
	wgpuList->BeginRenderPass(fb, viewport);
	wgpuList->SetGraphicsPipeline(pipeline);
	wgpuList->DrawIndirect(args, 0);
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
