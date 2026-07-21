#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

// The first shader-correctness test: a compute kernel writes a known pattern into a bindless
// RWStructuredBuffer, read back and checked exactly. Unlike the buffer-copy readback test, this
// exercises Slang->target codegen and bindless handle resolution (on Metal, the slot-index ->
// gpuAddress translation at dispatch). Backend-agnostic -- runs on D3D12 and Metal.
TEST_CASE("Compute dispatch writes a bindless buffer", "[compute]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.enablePixDebug           = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc = bgl::CommandListDesc();
	cmdListDesc.type = bgl::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	constexpr uint32_t kCount = 8;

	auto bufDesc = bgl::ComputeBufferDesc();
	bufDesc.SetElement<uint32_t>().SetMaxCount(kCount).SetDebugName("Compute Out Buffer");

	auto outBuf = resourceManager->CreateComputeBuffer(bufDesc);
	REQUIRE(resourceManager->ValidBufferHandle(outBuf));

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSComputeBufferTest"))
			.SetDebugName("CSComputeBufferTest"));

	kernel["gUniforms"]["outBuffer"] = outBuf;

	auto state   = bgl::ComputeState();
	state.kernel = &kernel;

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = kCount * sizeof(uint32_t);
	rbDesc.debugName = "Compute Readback";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);  // one threadgroup of 8 threads == kCount elements

	cmdList->Barrier(
		outBuf,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));

	cmdList->CopyBufferToReadback(rb, outBuf);
	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(rb));
	REQUIRE(mapped != nullptr);

	for (uint32_t i = 0; i < kCount; ++i)
	{
		CHECK(mapped[i] == i * 10u + 1u);
	}

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyBuffer(outBuf, false);
}

// A mixed cbuffer (float3 + scalar + handle) puts the bindless handle at a non-zero,
// alignment-dependent offset, exercising the layout recompute a single-field cbuffer does not.
TEST_CASE("Compute dispatch resolves a handle at a non-zero cbuffer offset", "[compute]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
	opts.enablePixDebug           = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc = bgl::CommandListDesc();
	cmdListDesc.type = bgl::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	constexpr uint32_t kCount = 8;

	auto bufDesc = bgl::ComputeBufferDesc();
	bufDesc.SetElement<uint32_t>().SetMaxCount(kCount).SetDebugName("Layout Out Buffer");

	auto outBuf = resourceManager->CreateComputeBuffer(bufDesc);
	REQUIRE(resourceManager->ValidBufferHandle(outBuf));

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSComputeLayoutTest"))
			.SetDebugName("CSComputeLayoutTest"));

	kernel["gUniforms"]["tint"]      = glm::vec3(2.0f, 0.0f, 0.0f);
	kernel["gUniforms"]["seed"]      = 5u;
	kernel["gUniforms"]["outBuffer"] = outBuf;

	auto state   = bgl::ComputeState();
	state.kernel = &kernel;

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = kCount * sizeof(uint32_t);
	rbDesc.debugName = "Layout Readback";

	auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

	cmdList->Open(cmdQueue, cmdAllocator);

	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);

	cmdList->Barrier(
		outBuf,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgl::BarrierAccessFlag::kCopySource));

	cmdList->CopyBufferToReadback(rb, outBuf);
	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(rb));
	REQUIRE(mapped != nullptr);

	// i*10 + seed(5) + tint.x(2)
	for (uint32_t i = 0; i < kCount; ++i)
	{
		CHECK(mapped[i] == i * 10u + 7u);
	}

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyBuffer(outBuf, false);
}
