#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "idl/TextureHandle.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>

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
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto  resourceManager = gfxBase->GetResourceManagerCpy();
	auto* device          = gfxBase->GetDevice();

	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	auto texDesc          = bgl::TextureDesc();
	texDesc.width         = 1;
	texDesc.height        = 1;
	texDesc.format        = bgl::Format::RGBA8_UNORM;
	texDesc.usage         = bgl::TextureUsageFlag::kSRV;
	texDesc.initialLayout = bgl::BarrierLayout::kCopyDest;
	texDesc.debugName     = "Struct Handle Source";

	const bgl::TextureHandle texture = resourceManager->CreateTexture(texDesc);
	REQUIRE(resourceManager->ValidTextureHandle(texture));

	auto srvDesc      = bgl::SrvDesc();
	srvDesc.format    = texDesc.format;
	srvDesc.dimension = texDesc.dimension;
	srvDesc.debugName = "Struct Handle Source SRV";

	const bgl::SrvHandle srv = resourceManager->CreateSrv(texture, srvDesc);
	REQUIRE(resourceManager->ValidSrvHandle(srv));

	// Deliberately not grey: a wrong channel order or a zeroed sample is visible in the result.
	const uint8_t               texel[4] = { 255, 128, 0, 255 };
	bgl::TextureSubresourceData sub{};
	sub.data       = texel;
	sub.rowPitch   = sizeof(texel);
	sub.slicePitch = sizeof(texel);
	std::array<bgl::TextureSubresourceData, 1> subresources{ sub };

	const bgl::SamplerHandle sampler = resourceManager->CreateSampler(bgl::SamplerDesc());
	REQUIRE(resourceManager->ValidSamplerHandle(sampler));

	// The handle the shader dereferences, resolved exactly as Scene resolves a material's.
	auto materialDesc         = bgl::ComputeBufferDesc();
	materialDesc.initialCount = 1;
	materialDesc.debugName    = "Struct-Resident Texture Handle";
	materialDesc.SetElement<bgl::idl::TextureHandle>();
	const bgl::BufferHandle materials = resourceManager->CreateComputeBuffer(materialDesc);
	REQUIRE(resourceManager->ValidBufferHandle(materials));

	const bgl::idl::TextureHandle stored{ srv.descriptor };

	auto outDesc         = bgl::ComputeBufferDesc();
	outDesc.initialCount = 1;
	outDesc.debugName    = "Sampled Colour";
	outDesc.SetElement<glm::vec4>();
	const bgl::BufferHandle outBuffer = resourceManager->CreateComputeBuffer(outDesc);
	REQUIRE(resourceManager->ValidBufferHandle(outBuffer));

	auto rbDesc                        = bgl::ReadbackBufferDesc();
	rbDesc.byteSize                    = sizeof(glm::vec4);
	rbDesc.debugName                   = "Sampled Colour Readback";
	const bgl::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
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
		bgl::TextureBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.SetLayoutBefore(bgl::BarrierLayout::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgl::BarrierAccessFlag::kShaderResource)
			.SetLayoutAfter(bgl::BarrierLayout::kShaderResource));
	cmdList->Barrier(
		materials,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgl::BarrierAccessFlag::kShaderResource));

	auto state   = bgl::ComputeState();
	state.kernel = &kernel;
	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		outBuffer,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));

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
