#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_wgpu.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"
#include "types/MeshletState.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

namespace
{
	struct Float2
	{
		float x;
		float y;
	};

	const Float2 c_Triangle[3] = { { 0.0f, 0.8f }, { -0.8f, -0.8f }, { 0.8f, -0.8f } };
}

// DispatchMeshIndirect through the RHI: SetMeshletState latches the framebuffer, viewport, kernel,
// and the indirect-args buffer an upstream expansion pass wrote; DispatchMeshIndirect opens the
// render pass, binds the kernel, and draws indirectly from that buffer -- the emulation the forward
// path runs where D3D12 dispatches a mesh shader. A compute kernel stands in for the expansion,
// writing the vertex stream and the DrawIndirectArgs; a green triangle proves the whole path.
TEST_CASE(
	"SetMeshletState then DispatchMeshIndirect draws through the meshlet RHI",
	"[wgpu][render]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	const auto posBuffer = resources->CreateComputeBuffer(
		ComputeBufferDesc{}.SetElement<Float2>().SetInitialCount(3).SetDebugName("positions"));
	const auto argsBuffer = resources->CreateComputeBuffer(
		ComputeBufferDesc{}.SetElement<uint32_t>().SetInitialCount(4).SetDebugName("draw-args"));

	auto expand = device->CreateComputeKernel(
		ComputePipelineDesc{}
			.SetShader(device->CreateShader(ShaderDesc{}.SetSlangModuleName("ExpandTest")))
			.SetDebugName("expand"));
	expand["gExpand"]["positions"] = posBuffer;
	expand["gExpand"]["drawArgs"]  = argsBuffer;

	auto computeState   = ComputeState{};
	computeState.kernel = &expand;

	const auto gfxShader = device->CreateShader(ShaderDesc{}.SetSlangModuleName("VertexPullTest"));
	auto       gfxDesc   = MeshletPipelineDesc{};
	gfxDesc.SetMeshShader(gfxShader).SetPixelShader(gfxShader).AddRtvFormat(Format::RGBA8_UNORM);
	gfxDesc.renderState.rasterState.SetCullNone();

	auto gfx                  = device->CreateMeshletKernel(gfxDesc);
	gfx["gPull"]["positions"] = posBuffer;

	auto texDesc      = TextureDesc{};
	texDesc.width     = c_Size;
	texDesc.height    = c_Size;
	texDesc.format    = Format::RGBA8_UNORM;
	texDesc.usage     = TextureUsage(TextureUsageFlag::kRenderTarget);
	texDesc.debugName = "rt";

	const auto texture = resources->CreateTexture(texDesc);
	const auto rtv     = resources->CreateRtv(texture, RtvDesc{ .format = Format::RGBA8_UNORM });

	auto state         = MeshletState{};
	state.kernel       = &gfx;
	state.frameBuffer  = FrameBuffer().AddColorAttachment(rtv);
	state.indirectArgs = argsBuffer;
	state.viewportState.AddViewportAndScissorRect(
		Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size)));

	const auto layout   = resources->GetTextureReadbackLayout(texture);
	const auto readback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = layout.totalBytes, .debugName = "rt-readback" });

	float clear[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

	list->Open(queue.Get(), allocator.Get());
	list->SetComputeState(computeState);
	list->Dispatch(1, 1, 1);
	resources->ClearRtv(list.Get(), rtv, clear);
	list->SetMeshletState(state);
	list->DispatchMeshIndirect(0);
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

// Plain DispatchMesh through the RHI: the fullscreen/skybox path, one triangle per thread group.
// DispatchMesh(1, 1, 1) becomes a three-vertex draw of the vertex-pulling kernel, whose bound buffer
// holds the triangle -- a green centre over a red corner proves the draw ran.
TEST_CASE("SetMeshletState then DispatchMesh draws one triangle per thread group", "[wgpu][render]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	const auto posBuffer = resources->CreateComputeBuffer(
		ComputeBufferDesc{}.SetElement<Float2>().SetInitialCount(3).SetDebugName("positions"));

	const auto shader = device->CreateShader(ShaderDesc{}.SetSlangModuleName("VertexPullTest"));

	auto desc = MeshletPipelineDesc{};
	desc.SetMeshShader(shader).SetPixelShader(shader).AddRtvFormat(Format::RGBA8_UNORM);
	desc.renderState.rasterState.SetCullNone();

	auto kernel                  = device->CreateMeshletKernel(desc);
	kernel["gPull"]["positions"] = posBuffer;

	auto texDesc      = TextureDesc{};
	texDesc.width     = c_Size;
	texDesc.height    = c_Size;
	texDesc.format    = Format::RGBA8_UNORM;
	texDesc.usage     = TextureUsage(TextureUsageFlag::kRenderTarget);
	texDesc.debugName = "rt";

	const auto texture = resources->CreateTexture(texDesc);
	const auto rtv     = resources->CreateRtv(texture, RtvDesc{ .format = Format::RGBA8_UNORM });

	auto state        = MeshletState{};
	state.kernel      = &kernel;
	state.frameBuffer = FrameBuffer().AddColorAttachment(rtv);
	state.viewportState.AddViewportAndScissorRect(
		Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size)));

	const auto layout   = resources->GetTextureReadbackLayout(texture);
	const auto readback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = layout.totalBytes, .debugName = "rt-readback" });

	float clear[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

	list->Open(queue.Get(), allocator.Get());
	list->WriteBuffer(posBuffer, c_Triangle, 0, sizeof(c_Triangle));
	resources->ClearRtv(list.Get(), rtv, clear);
	list->SetMeshletState(state);
	list->DispatchMesh(1, 1, 1);
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
