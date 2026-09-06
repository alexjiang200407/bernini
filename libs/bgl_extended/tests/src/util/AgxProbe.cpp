#include "util/AgxProbe.h"
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "types/Barrier.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <core/glm.h>

namespace bgl::test
{
	float
	EncodeSrgb(float linear) noexcept
	{
		return linear <= 0.0031308f ? linear * 12.92f :
		                              1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
	}

	glm::vec4
	RunAgX(bgl::IGraphics& gfx, float sceneLinear)
	{
		auto* gfxBase = gfx.As<bgl::GraphicsBase>();
		REQUIRE(gfxBase != nullptr);

		auto  resourceManager = gfxBase->GetResourceManagerCpy();
		auto* device          = gfxBase->GetDevice();

		auto cmdListDesc  = bgl::CommandListDesc();
		cmdListDesc.type  = bgl::QueueType::kGraphics;
		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

		auto outDesc         = bgl::ComputeBufferDesc();
		outDesc.initialCount = 1;
		outDesc.debugName    = "AgX Result";
		outDesc.SetElement<glm::vec4>();
		const bgl::BufferHandle outBuffer = resourceManager->CreateComputeBuffer(outDesc);
		REQUIRE(resourceManager->ValidBufferHandle(outBuffer));

		auto rbDesc                        = bgl::ReadbackBufferDesc();
		rbDesc.byteSize                    = sizeof(glm::vec4);
		rbDesc.debugName                   = "AgX Readback";
		const bgl::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

		auto kernel = device->CreateComputeKernel(
			bgl::ComputePipelineDesc()
				.SetShader(device->CreateShader("CSAgxCalibration"))
				.SetDebugName("AgX Calibration"));
		REQUIRE(kernel.pipeline != nullptr);
		REQUIRE(kernel.uniforms.contains("gUniforms"));

		kernel["gUniforms"]["outColor"]    = outBuffer;
		kernel["gUniforms"]["sceneLinear"] = sceneLinear;

		cmdList->Open(cmdQueue, cmdAllocator);

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

		const auto* mapped = static_cast<const glm::vec4*>(resourceManager->MapReadback(rb));
		REQUIRE(mapped != nullptr);
		const glm::vec4 result = *mapped;

		resourceManager->UnmapReadback(rb);
		resourceManager->DestroyReadbackBuffer(rb, false);
		resourceManager->DestroyBuffer(outBuffer, false);

		return result;
	}
}
