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
}

// Plain-data members of a constant buffer, which WGSL gathers into a uniform buffer bound alongside
// the storage buffers rather than into a descriptor heap. The shader echoes the scalars back and
// reads two off-diagonal matrix elements, which pins the element mapping: glm stores column-major
// (m[col][row]) and Slang indexes [row][col], so a transposed upload would swap the two readbacks
// rather than go unnoticed.
TEST_CASE("A compute kernel reads scalars and a matrix from a uniform block", "[wgpu][compute]")
{
	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	const auto outBuffer = resources->CreateComputeBuffer(
		ComputeBufferDesc{}.SetElement<uint32_t>().SetInitialCount(4).SetDebugName("out"));

	auto kernel = device->CreateComputeKernel(
		ComputePipelineDesc{}
			.SetShader(device->CreateShader(ShaderDesc{}.SetSlangModuleName("MixedUniformTest")))
			.SetDebugName("mixed"));

	auto transform  = glm::mat4(1.0f);
	transform[3][0] = 7.0f;  // column 3, row 0
	transform[0][3] = 9.0f;  // column 0, row 3

	kernel["gMixed"]["viewProj"]   = transform;
	kernel["gMixed"]["psoIndex"]   = 11u;
	kernel["gMixed"]["baseSource"] = 22u;
	kernel["gMixed"]["outBuffer"]  = outBuffer;

	auto computeState   = ComputeState{};
	computeState.kernel = &kernel;

	const auto readback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = 4 * sizeof(uint32_t), .debugName = "out-readback" });

	list->Open(queue.Get(), allocator.Get());
	list->SetComputeState(computeState);
	list->Dispatch(1, 1, 1);
	list->CopyBufferToReadback(readback, outBuffer);
	list->Close();

	const auto fence = queue->ExecuteCommandList(list.Get());
	queue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(resources->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	CHECK(mapped[0] == 11u);
	CHECK(mapped[1] == 22u);

	// The shader reads viewProj[3][0] then viewProj[0][3] -- row-major indices, so they pick up the
	// column-major writes above crosswise.
	CHECK(mapped[2] == 9u);
	CHECK(mapped[3] == 7u);

	resources->UnmapReadback(readback);
	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}

// The same mixed constant buffer read from the *vertex* stage, which is what ForwardData needs: the
// uniform block must be visible there, not only in compute. The triangle is pushed off screen by the
// offset and pulled back by the matrix, so a green centre proves both members arrived -- either one
// dropped or mis-transformed leaves the clear colour.
TEST_CASE("A vertex shader reads a matrix and vector from a uniform block", "[wgpu][render]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	// Shifted a full unit off screen, so the draw only lands if the matrix undoes exactly it.
	const Float2 positions[3] = { { 1.0f, 1.8f }, { 0.2f, 0.2f }, { 1.8f, 0.2f } };

	const auto posBuffer = resources->CreateComputeBuffer(
		ComputeBufferDesc{}.SetElement<Float2>().SetInitialCount(3).SetDebugName("positions"));

	auto desc = MeshletPipelineDesc{};
	desc.SetMeshShader(device->CreateShader("UniformDrawTest", "VSMain"))
		.SetPixelShader(device->CreateShader("UniformDrawTest", "PSMain"))
		.AddRtvFormat(Format::RGBA8_UNORM);
	desc.renderState.rasterState.SetCullNone();

	auto kernel = device->CreateMeshletKernel(desc);

	// offset moves the triangle to (-1..) and the translation puts it back around the origin.
	auto transform  = glm::mat4(1.0f);
	transform[3][0] = 0.0f;
	transform[3][1] = -1.0f;

	kernel["gDraw"]["transform"] = transform;
	kernel["gDraw"]["offset"]    = glm::vec2(-1.0f, 0.0f);
	kernel["gDraw"]["positions"] = posBuffer;

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
	list->WriteBuffer(posBuffer, positions, 0, sizeof(positions));
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
