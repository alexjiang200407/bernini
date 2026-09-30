#include "gfx/GraphicsBase.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/IGraphics.h>
#include <bgl_common/idl/RawTextureHandle.h>
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
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
	// The record sits past the start so a wrong base cannot pass, and the handle sits inside it.
	constexpr uint32_t c_RecordOffset = 32;
	constexpr uint32_t c_HandleOffset = c_RecordOffset + 16;
	constexpr uint32_t c_ArenaBytes   = 64;
}

/**
 * One allocation, read as bytes and as texture handles at the same time.
 *
 * This is what lets a record keep a resource handle inside it. A raw view cannot make a texture of
 * the bytes it reads -- on Metal the element type of a bindless buffer is fixed at its declaration,
 * and a raw one declares bytes -- so the record stores a `RawTextureHandle`, which declares no
 * resource type and is therefore loadable, and a second, typed view of the same allocation is what
 * turns those bytes into something samplable.
 *
 * Nothing in bgl binds a second view yet; the material arena is what will. The test is the caller
 * so the mechanism is proven before anything rests on it.
 */
TEST_CASE("One buffer reads as bytes and as handles at once", "[twoview][compute][bindless]")
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
	texDesc.debugName     = "Typed View Texture";

	const bgpu::TextureHandle texture = resourceManager->CreateTexture(texDesc);
	REQUIRE(resourceManager->ValidTextureHandle(texture));

	auto srvDesc      = bgpu::SrvDesc();
	srvDesc.format    = texDesc.format;
	srvDesc.dimension = texDesc.dimension;
	srvDesc.debugName = "Typed View Texture SRV";

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

	const auto                   tint   = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);
	const bgpu::DescriptorHandle stored = srv.descriptor;

	static_assert(sizeof(bgl::idl::RawTextureHandle) == sizeof(bgpu::DescriptorHandle));

	std::array<std::byte, c_ArenaBytes> bytes{};
	std::memcpy(bytes.data() + c_RecordOffset, &tint, sizeof(tint));
	std::memcpy(bytes.data() + c_HandleOffset, &stored, sizeof(stored));

	const bgpu::BufferHandle arena = resourceManager->CreateRawBuffer(
		bgpu::RawViewDesc().SetByteSize(c_ArenaBytes).SetDebugName("Typed View Arena"));
	REQUIRE(resourceManager->ValidBufferHandle(arena));

	// The second view: the same allocation, read as handles.
	const bgpu::BufferSrvHandle handles = resourceManager->CreateBufferSrv(
		arena,
		bgpu::BufferSrvDesc().SetElement<bgpu::DescriptorHandle>().SetDebugName("Typed View"));
	REQUIRE(resourceManager->ValidBufferSrvHandle(handles));

	auto outDesc         = bgpu::ComputeBufferDesc();
	outDesc.initialCount = 3;
	outDesc.debugName    = "Typed View Results";
	outDesc.SetElement<glm::vec4>();
	const bgpu::BufferHandle outValues = resourceManager->CreateComputeBuffer(outDesc);

	auto rbDesc                         = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize                     = 3 * sizeof(glm::vec4);
	rbDesc.debugName                    = "Typed View Readback";
	const bgpu::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

	auto kernel = device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSTypedViewRead"))
			.SetDebugName("Typed View Read"));
	REQUIRE(kernel.pipeline != nullptr);

	kernel["gUniforms"]["arena"]            = arena;
	kernel["gUniforms"]["handles"]          = handles;
	kernel["gUniforms"]["outValues"]        = outValues;
	kernel["gUniforms"]["samp"]             = sampler;
	kernel["gUniforms"]["recordOffset"]     = c_RecordOffset;
	kernel["gUniforms"]["handleByteOffset"] = c_HandleOffset;

	cmdList->Open(cmdQueue, cmdAllocator);

	cmdList->WriteBuffer(arena, bytes.data(), 0, bytes.size());
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
		arena,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kShaderResource));

	auto computeState   = bgpu::ComputeState();
	computeState.kernel = &kernel;
	cmdList->SetComputeState(computeState);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		outValues,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));

	cmdList->CopyBufferToReadback(rb, outValues);
	cmdList->Close();

	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

	const auto* got = static_cast<const glm::vec4*>(resourceManager->MapReadback(rb));
	REQUIRE(got != nullptr);

	// The raw view read the record, handle field included, without declaring a resource type.
	CHECK(got[0].x == Catch::Approx(tint.x));
	CHECK(got[0].y == Catch::Approx(tint.y));
	CHECK(got[0].z == Catch::Approx(tint.z));
	CHECK(got[0].w == Catch::Approx(tint.w));

	// And it read them at the offset the typed view samples from -- the agreement the whole design
	// rests on. Without this the two halves could be reading different bytes and still pass.
	auto expectedBits = glm::uvec2();
	std::memcpy(&expectedBits, &stored, sizeof(expectedBits));
	CHECK(static_cast<uint32_t>(got[1].x) == expectedBits.x);
	CHECK(static_cast<uint32_t>(got[1].y) == expectedBits.y);

	// Those same bytes are a live texture through the typed view.
	CHECK(got[2].x == Catch::Approx(1.0f).margin(0.01));
	CHECK(got[2].y == Catch::Approx(128.0f / 255.0f).margin(0.01));
	CHECK(got[2].z == Catch::Approx(0.0f).margin(0.01));

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyBuffer(outValues, false);
	resourceManager->DestroyBufferSrv(handles, false);
	resourceManager->DestroyBuffer(arena, false);
	resourceManager->DestroySampler(sampler, false);
	resourceManager->DestroySrv(srv, false);
	resourceManager->DestroyTexture(texture, false);
}
