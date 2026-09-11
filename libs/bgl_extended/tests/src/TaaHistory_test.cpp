#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "types/Barrier.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <array>
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstdint>

TEST_CASE(
	"Depth rejection distinguishes opaque disocclusion from uncertain history",
	"[taa][render][taaghosting]")
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

	constexpr uint32_t c_Count = 19;

	auto bufDesc = bgl::ComputeBufferDesc();
	bufDesc.SetElement<glm::vec2>().SetInitialCount(c_Count).SetDebugName("Compute Out Buffer");

	auto outBuf = resourceManager->CreateComputeBuffer(bufDesc);
	REQUIRE(resourceManager->ValidBufferHandle(outBuf));

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSTaaHistoryTest"))
			.SetDebugName("CSTaaHistoryTest"));

	kernel["gUniforms"]["results"] = outBuf;

	auto state   = bgl::ComputeState();
	state.kernel = &kernel;

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = c_Count * sizeof(glm::vec2);
	rbDesc.debugName = "Compute Readback";

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

	const auto* mapped = static_cast<const glm::vec2*>(resourceManager->MapReadback(rb));
	REQUIRE(mapped != nullptr);

	constexpr std::array<const char*, c_Count> c_Scenarios = { "opaque disocclusion",
		                                                       "matching depth",
		                                                       "uncertain current coverage",
		                                                       "uncertain history",
		                                                       "partially uncertain footprint",
		                                                       "partially visible footprint",
		                                                       "invalid history",
		                                                       "invalid camera pair",
		                                                       "resting camera",
		                                                       "independent object motion",
		                                                       "unrepresentable depth",
		                                                       "camera depth change",
		                                                       "uninitialised history",
		                                                       "depth quantisation margin",
		                                                       "offscreen history",
		                                                       "upscaled disocclusion",
		                                                       "bounded reconstruction weight",
		                                                       "rounded camera velocity",
		                                                       "object motion above quantisation" };
	for (uint32_t i = 0; i < c_Count; ++i)
	{
		INFO(c_Scenarios[i]);
		const bool reset = i == 0 || i == 6 || i == 12 || i == 14 || i == 15 || i == 16 || i == 17;
		CHECK(mapped[i].x == Catch::Approx(reset ? 0.2f : 0.77f).margin(1e-5f));
		const bool unavailable = i == 2 || i == 7 || i == 10;
		CHECK(mapped[i].y == Catch::Approx(unavailable ? 0.0f : 0.6f).margin(1e-5f));
	}

	resourceManager->UnmapReadback(rb);

	resourceManager->DestroyReadbackBuffer(rb, false);
	resourceManager->DestroyBuffer(outBuf, false);
}
