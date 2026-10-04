#pragma once
#include "gfx/GraphicsBase.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgl/glm.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

// A one-thread compute kernel run against a raw buffer of records, its float4 answers read back: how
// a test pins a shader function's numbers without a scene.

namespace bgl::test
{
	/**
	 * Runs `shader`'s one-thread compute entry over a raw buffer holding `records` (bound as
	 * `gUniforms.records` when non-empty) and returns the `outCount` values it wrote to
	 * `gUniforms.outValues`. `bind` sets the shader's other uniforms.
	 */
	inline std::vector<glm::vec4>
	DispatchReport(
		const std::string&                               shader,
		const std::span<const std::byte>                 records,
		const uint32_t                                   outCount,
		const std::function<void(bgpu::ComputeKernel&)>& bind)
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

		auto recordBuffer = bgpu::BufferHandle();
		if (!records.empty())
		{
			recordBuffer = resourceManager->CreateRawBuffer(
				bgpu::RawViewDesc()
					.SetByteSize(static_cast<uint32_t>(records.size()))
					.SetDebugName("Test Records"));
			REQUIRE(resourceManager->ValidBufferHandle(recordBuffer));
		}

		auto outDesc         = bgpu::ComputeBufferDesc();
		outDesc.initialCount = outCount;
		outDesc.debugName    = "Test Results";
		outDesc.SetElement<glm::vec4>();
		const bgpu::BufferHandle outValues = resourceManager->CreateComputeBuffer(outDesc);
		REQUIRE(resourceManager->ValidBufferHandle(outValues));

		auto rbDesc                         = bgpu::ReadbackBufferDesc();
		rbDesc.byteSize                     = outCount * sizeof(glm::vec4);
		rbDesc.debugName                    = "Test Readback";
		const bgpu::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

		auto kernel = device->CreateComputeKernel(
			bgpu::ComputePipelineDesc()
				.SetShader(device->CreateShader(shader))
				.SetDebugName(shader));
		REQUIRE(kernel.pipeline != nullptr);

		if (!records.empty())
		{
			kernel["gUniforms"]["records"] = recordBuffer;
		}
		kernel["gUniforms"]["outValues"] = outValues;
		bind(kernel);

		cmdList->Open(cmdQueue, cmdAllocator);

		if (!records.empty())
		{
			cmdList->WriteBuffer(recordBuffer, records.data(), 0, records.size());
			cmdList->Barrier(
				recordBuffer,
				bgpu::BufferBarrierDesc()
					.AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
					.AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
					.AddSyncAfter(bgpu::BarrierSyncFlag::kComputeShader)
					.AddAccessAfter(bgpu::BarrierAccessFlag::kShaderResource));
		}

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

		const auto* mapped = static_cast<const glm::vec4*>(resourceManager->MapReadback(rb));
		REQUIRE(mapped != nullptr);
		auto got = std::vector<glm::vec4>(mapped, mapped + outCount);
		resourceManager->UnmapReadback(rb);

		resourceManager->DestroyReadbackBuffer(rb, false);
		resourceManager->DestroyBuffer(outValues, false);
		if (!records.empty())
		{
			resourceManager->DestroyBuffer(recordBuffer, false);
		}
		return got;
	}
}
