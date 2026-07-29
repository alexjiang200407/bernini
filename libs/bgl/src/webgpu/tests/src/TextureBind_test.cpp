#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "pipeline/ComputeKernel.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"

#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// A texture and a sampler reaching a WGSL shader through the RHI. Everything below this works
// already -- textures upload, samplers create, buffers bind -- so what is under test is the one
// piece that did not: reflection classifying a Uniforms struct's leaves into texture, sampler and
// storage-buffer bind-group entries, and the command list resolving each to the right object.
//
// Asserted on the sampled value rather than on the pipeline building, because a texture bound at the
// wrong binding, or a view of the wrong dimension, still builds and then samples something else.
TEST_CASE("A shader samples a texture bound through the RHI", "[wgpu][texture][binding]")
{
	constexpr uint32_t c_Size = 4;

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
	texDesc.usage     = TextureUsageFlag::kSRV;
	texDesc.debugName = "bound";

	const auto texture = resources->CreateTexture(texDesc);
	REQUIRE(resources->ValidTextureHandle(texture));
	CHECK_FALSE(resources->IsTextureCube(texture));

	// A uniform colour, so the sampled result does not depend on which texel the filter lands on.
	// 0x8040C020 is ABGR in memory order: r=0x20, g=0xC0, b=0x40, a=0x80.
	const auto pixels = std::vector<uint32_t>(c_Size * c_Size, 0x8040C020u);

	auto sub       = TextureSubresourceData{};
	sub.data       = pixels.data();
	sub.rowPitch   = c_Size * 4;
	sub.slicePitch = sub.rowPitch * c_Size;

	const auto sampler = resources->CreateSampler(
		SamplerDesc().SetAllFilters(true).SetAllAddressModes(SamplerAddressMode::kClamp));
	REQUIRE(resources->ValidSamplerHandle(sampler));

	const auto output = resources->CreateComputeBuffer(
		ComputeBufferDesc{}.SetElement<float[4]>().SetInitialCount(1).SetDebugName("sampled"));

	auto kernel = device->CreateComputeKernel(
		ComputePipelineDesc{}
			.SetShader(device->CreateShader(ShaderDesc{}.SetSlangModuleName("CSTextureBindTest")))
			.SetDebugName("CSTextureBindTest"));

	kernel["gUniforms"]["texture"]   = texture;
	kernel["gUniforms"]["sampler"]   = sampler;
	kernel["gUniforms"]["outBuffer"] = output;

	auto state   = ComputeState{};
	state.kernel = &kernel;

	const auto readback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = 4 * sizeof(float), .debugName = "sampled-readback" });

	list->Open(queue.Get(), allocator.Get());
	list->WriteTexture(texture, std::span<const TextureSubresourceData>(&sub, 1));
	list->SetComputeState(state);
	list->Dispatch(1, 1, 1);
	list->CopyBufferToReadback(readback, output);
	list->Close();

	const auto fence = queue->ExecuteCommandList(list.Get());
	queue->WaitForFenceCPUBlocking(fence);

	const auto* sampled = static_cast<const float*>(resources->MapReadback(readback));
	REQUIRE(sampled != nullptr);

	// RGBA8_UNORM, so each byte divided by 255. A wrong binding reads zeros or another resource.
	constexpr float c_Tolerance = 1.f / 255.f;
	CHECK(std::abs(sampled[0] - 0x20 / 255.f) < c_Tolerance);
	CHECK(std::abs(sampled[1] - 0xC0 / 255.f) < c_Tolerance);
	CHECK(std::abs(sampled[2] - 0x40 / 255.f) < c_Tolerance);
	CHECK(std::abs(sampled[3] - 0x80 / 255.f) < c_Tolerance);

	resources->UnmapReadback(readback);

	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}
