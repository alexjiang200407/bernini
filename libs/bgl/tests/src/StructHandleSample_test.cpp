#include "gfx/GraphicsBase.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/IGraphics.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/uniforms/DescriptorHandle.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

/**
 * A texture handle stored *inside GPU memory* still resolves to its texture.
 *
 * The sibling test in TextureSample_test.cpp binds its handle through a constant buffer, which the
 * Metal backend rewrites to a native resource id on every dispatch -- so it passes whatever
 * ResolveDescriptor does. A material's texture handle is not like that: the CPU writes it into a
 * struct buffer once and the shader dereferences whatever it finds, with no per-dispatch rewrite to
 * correct it. That is the path this test covers, and the only one that can catch a descriptor
 * written as something the shader cannot dereference.
 *
 * On D3D12 the slot doubles as the heap index, so this passes either way; it earns its keep on
 * backends where the two differ.
 */
TEST_CASE(
	"A texture handle stored in a struct buffer resolves to the sampled texel",
	"[texture][compute][bindless]")
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

	auto cmdListDesc  = bgpu::CommandListDesc();
	cmdListDesc.type  = bgpu::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

	auto texDesc          = bgpu::TextureDesc();
	texDesc.width         = 1;
	texDesc.height        = 1;
	texDesc.format        = bgpu::Format::RGBA8_UNORM;
	texDesc.usage         = bgpu::TextureUsageFlag::kSRV;
	texDesc.initialLayout = bgpu::BarrierLayout::kCopyDest;
	texDesc.debugName     = "Struct Handle Source";

	const bgpu::TextureHandle texture = resourceManager->CreateTexture(texDesc);
	REQUIRE(resourceManager->ValidTextureHandle(texture));

	auto srvDesc      = bgpu::SrvDesc();
	srvDesc.format    = texDesc.format;
	srvDesc.dimension = texDesc.dimension;
	srvDesc.debugName = "Struct Handle Source SRV";

	const bgpu::SrvHandle srv = resourceManager->CreateSrv(texture, srvDesc);
	REQUIRE(resourceManager->ValidSrvHandle(srv));

	// Deliberately not grey: a wrong channel order or a zeroed sample is visible in the result.
	const uint8_t                texel[4] = { 255, 128, 0, 255 };
	bgpu::TextureSubresourceData sub{};
	sub.data       = texel;
	sub.rowPitch   = sizeof(texel);
	sub.slicePitch = sizeof(texel);
	std::array<bgpu::TextureSubresourceData, 1> subresources{ sub };

	const bgpu::SamplerHandle sampler = resourceManager->CreateSampler(bgpu::SamplerDesc());
	REQUIRE(resourceManager->ValidSamplerHandle(sampler));

	// The handle the shader dereferences, resolved exactly as Scene resolves a material's.
	auto materialDesc         = bgpu::ComputeBufferDesc();
	materialDesc.initialCount = 1;
	materialDesc.debugName    = "Struct-Resident Texture Handle";
	materialDesc.SetElement<bgpu::DescriptorHandle>();
	const bgpu::BufferHandle materials = resourceManager->CreateComputeBuffer(materialDesc);
	REQUIRE(resourceManager->ValidBufferHandle(materials));

	const bgpu::DescriptorHandle stored = srv.descriptor;

	auto outDesc         = bgpu::ComputeBufferDesc();
	outDesc.initialCount = 1;
	outDesc.debugName    = "Sampled Colour";
	outDesc.SetElement<glm::vec4>();
	const bgpu::BufferHandle outBuffer = resourceManager->CreateComputeBuffer(outDesc);
	REQUIRE(resourceManager->ValidBufferHandle(outBuffer));

	auto rbDesc                         = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize                     = sizeof(glm::vec4);
	rbDesc.debugName                    = "Sampled Colour Readback";
	const bgpu::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

	auto kernel = device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSStructHandleSample"))
			.SetDebugName("Struct Handle Sample"));
	REQUIRE(kernel.pipeline != nullptr);
	REQUIRE(kernel.uniforms.contains("gUniforms"));

	kernel["gUniforms"]["materials"] = materials;
	kernel["gUniforms"]["sampler"]   = sampler;
	kernel["gUniforms"]["outColor"]  = outBuffer;

	cmdList->Open(cmdQueue, cmdAllocator);

	cmdList->WriteBuffer(materials, &stored, 0, sizeof(stored));
	cmdList->WriteTexture(texture, subresources);
	cmdList->Barrier(
		texture,
		bgpu::TextureBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
			.SetLayoutBefore(bgpu::BarrierLayout::kCopyDest)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kShaderResource)
			.SetLayoutAfter(bgpu::BarrierLayout::kShaderResource));
	cmdList->Barrier(
		materials,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kShaderResource));

	auto state   = bgpu::ComputeState();
	state.kernel = &kernel;
	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		outBuffer,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));

	cmdList->CopyBufferToReadback(rb, outBuffer);
	cmdList->Close();

	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

	const auto* sampled = static_cast<const glm::vec4*>(resourceManager->MapReadback(rb));
	REQUIRE(sampled != nullptr);

	CHECK(sampled->r == Catch::Approx(1.0f).margin(0.01));
	CHECK(sampled->g == Catch::Approx(128.0f / 255.0f).margin(0.01));
	CHECK(sampled->b == Catch::Approx(0.0f).margin(0.01));
	CHECK(sampled->a == Catch::Approx(1.0f).margin(0.01));

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyBuffer(outBuffer, false);
	resourceManager->DestroyBuffer(materials, false);
	resourceManager->DestroySampler(sampler, false);
	resourceManager->DestroySrv(srv, false);
	resourceManager->DestroyTexture(texture, false);
}
