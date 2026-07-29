#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_wgpu.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "resource/ResourceManager.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// Vertex pulling through the RHI: the vertex shader reads its positions from a storage buffer bound
// by the meshlet kernel's uniforms -- the same handle-write-to-bind-group path Dispatch uses for
// compute -- not from a vertex buffer, which is the shape the forward path draws with. The buffer is
// filled with a triangle; a green centre over a red corner proves the VS read the bound buffer.
TEST_CASE("A vertex shader pulls positions from a bound buffer", "[wgpu][render]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	struct Float2
	{
		float x;
		float y;
	};
	const Float2 positions[3] = { { 0.0f, 0.8f }, { -0.8f, -0.8f }, { 0.8f, -0.8f } };

	const auto posBuffer = resources->CreateComputeBuffer(
		ComputeBufferDesc{}.SetElement<Float2>().SetInitialCount(3).SetDebugName("positions"));

	const auto shader = device->CreateShader(ShaderDesc{}.SetSlangModuleName("VertexPullTest"));

	auto desc = MeshletPipelineDesc{};
	desc.SetMeshShader(shader).SetPixelShader(shader).AddRtvFormat(Format::RGBA8_UNORM);
	desc.renderState.rasterState.SetCullNone();

	auto kernel = device->CreateMeshletKernel(desc, resources);
	REQUIRE(kernel.ContainsUniforms("gPull"));
	kernel["gPull"]["positions"] = posBuffer;

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
	list->WriteBuffer(posBuffer, positions, 0, sizeof(positions));
	resources->ClearRtv(list.Get(), rtv, clear);
	wgpuList->BeginRenderPass(fb, viewport);
	wgpuList->SetGraphicsKernel(kernel);
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
