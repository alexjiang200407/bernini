#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "idl/RawTextureHandle.h"
#include "idl/TextureHandle.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>

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
	texDesc.debugName     = "Typed View Texture";

	const bgl::TextureHandle texture = resourceManager->CreateTexture(texDesc);
	REQUIRE(resourceManager->ValidTextureHandle(texture));

	auto srvDesc      = bgl::SrvDesc();
	srvDesc.format    = texDesc.format;
	srvDesc.dimension = texDesc.dimension;
	srvDesc.debugName = "Typed View Texture SRV";

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

	const auto tint   = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);
	const auto stored = bgl::idl::TextureHandle{ srv.descriptor };

	static_assert(sizeof(bgl::idl::RawTextureHandle) == sizeof(bgl::idl::TextureHandle));

	std::array<std::byte, c_ArenaBytes> bytes{};
	std::memcpy(bytes.data() + c_RecordOffset, &tint, sizeof(tint));
	std::memcpy(bytes.data() + c_HandleOffset, &stored, sizeof(stored));

	const bgl::BufferHandle arena = resourceManager->CreateRawBuffer(
		bgl::RawViewDesc().SetByteSize(c_ArenaBytes).SetDebugName("Typed View Arena"));
	REQUIRE(resourceManager->ValidBufferHandle(arena));

	// The second view: the same allocation, read as handles.
	const bgl::BufferSrvHandle handles = resourceManager->CreateBufferSrv(
		arena,
		bgl::BufferSrvDesc().SetElement<bgl::idl::TextureHandle>().SetDebugName("Typed View"));
	REQUIRE(resourceManager->ValidBufferSrvHandle(handles));

	auto outDesc         = bgl::ComputeBufferDesc();
	outDesc.initialCount = 3;
	outDesc.debugName    = "Typed View Results";
	outDesc.SetElement<glm::vec4>();
	const bgl::BufferHandle outValues = resourceManager->CreateComputeBuffer(outDesc);

	auto rbDesc                        = bgl::ReadbackBufferDesc();
	rbDesc.byteSize                    = 3 * sizeof(glm::vec4);
	rbDesc.debugName                   = "Typed View Readback";
	const bgl::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
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
		bgl::TextureBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.SetLayoutBefore(bgl::BarrierLayout::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgl::BarrierAccessFlag::kShaderResource)
			.SetLayoutAfter(bgl::BarrierLayout::kShaderResource));
	cmdList->Barrier(
		arena,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kCopy)
			.AddAccessBefore(bgl::BarrierAccessFlag::kCopyDest)
			.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgl::BarrierAccessFlag::kShaderResource));

	auto computeState   = bgl::ComputeState();
	computeState.kernel = &kernel;
	cmdList->SetComputeState(computeState);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		outValues,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));

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
