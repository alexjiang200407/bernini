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

// The shader cache salt folds the Slang build tag, which is read through the free function so that
// computing a salt never creates a global session. If the two ever disagreed, every shader cache
// written by an earlier build would silently miss and recompile, which is invisible except as a
// slow startup -- so the equivalence is pinned here rather than left to the API docs.
TEST_CASE("The free Slang build tag matches the global session's", "[slang]")
{
	Slang::ComPtr<slang::IGlobalSession> globalSession;
	REQUIRE(SLANG_SUCCEEDED(slang::createGlobalSession(globalSession.writeRef())));
	REQUIRE(globalSession != nullptr);

	CHECK(std::string(spGetBuildTagString()) == std::string(globalSession->getBuildTagString()));
}

// CreateGraphics drops the Slang session once it has built every renderer PSO, so a kernel created
// afterwards is the first thing to need a session again and must transparently get a new one. Run
// twice against one cache directory: the second pass is the load-bearing one, because a warm cache
// means construction compiled nothing at all and the session being recreated here never existed.
TEST_CASE("A compute kernel built after device creation recreates the Slang session", "[compute]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	for (int pass = 0; pass < 2; ++pass)
	{
		CAPTURE(pass);

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
		bufDesc.SetElement<uint32_t>().SetInitialCount(kCount).SetDebugName("Slang Session Out");

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
		rbDesc.debugName = "Slang Session Readback";

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

		for (uint32_t i = 0; i < kCount; ++i)
		{
			CHECK(mapped[i] == i * 10u + 1u);
		}

		resourceManager->UnmapReadback(rb);

		resourceManager->DestroyReadbackBuffer(rb, false);
		resourceManager->DestroyBuffer(outBuf, false);
	}
}
