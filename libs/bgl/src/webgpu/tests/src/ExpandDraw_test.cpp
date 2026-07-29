#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_wgpu.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The dispatch-expand -> drawIndirect loop the WebGPU mesh emulation runs in place of a mesh shader:
// a compute kernel writes a vertex stream and the indirect draw arguments, then the raster stage
// pulls those vertices and draws indirectly from those arguments -- all on one command list, so the
// compute writes are ordered before the draw reads them. A green triangle proves the whole chain: the
// compute output feeds both the VS's bound buffer and drawIndirect's vertex count.
TEST_CASE("A compute kernel expands geometry a draw pulls and draws indirectly", "[wgpu][render]")
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

	const auto posBuffer = resources->CreateComputeBuffer(
		ComputeBufferDesc{}.SetElement<Float2>().SetInitialCount(3).SetDebugName("positions"));
	const auto argsBuffer = resources->CreateComputeBuffer(
		ComputeBufferDesc{}.SetElement<uint32_t>().SetInitialCount(4).SetDebugName("draw-args"));

	auto expand = device->CreateComputeKernel(
		ComputePipelineDesc{}
			.SetShader(device->CreateShader(ShaderDesc{}.SetSlangModuleName("ExpandTest")))
			.SetDebugName("expand"),
		resources);
	expand["gExpand"]["positions"] = posBuffer;
	expand["gExpand"]["drawArgs"]  = argsBuffer;

	auto computeState   = ComputeState{};
	computeState.kernel = &expand;

	const auto gfxShader = device->CreateShader(ShaderDesc{}.SetSlangModuleName("VertexPullTest"));
	auto       gfxDesc   = MeshletPipelineDesc{};
	gfxDesc.SetMeshShader(gfxShader).SetPixelShader(gfxShader).AddRtvFormat(Format::RGBA8_UNORM);
	gfxDesc.renderState.rasterState.SetCullNone();

	auto gfx                  = device->CreateMeshletKernel(gfxDesc, resources);
	gfx["gPull"]["positions"] = posBuffer;

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
	list->SetComputeState(computeState);
	list->Dispatch(1, 1, 1);
	resources->ClearRtv(list.Get(), rtv, clear);
	wgpuList->BeginRenderPass(fb, viewport);
	wgpuList->SetGraphicsKernel(gfx);
	wgpuList->DrawIndirect(argsBuffer, 0);
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
