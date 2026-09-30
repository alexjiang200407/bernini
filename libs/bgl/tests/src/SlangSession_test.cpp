#include "gfx/GraphicsBase.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>

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

// CreateGraphics drops the Slang sessions once it has built every renderer PSO, so a kernel created
// afterwards is the first thing to need a session again and must transparently get a new one. Run
// twice against one cache directory: the second pass is the load-bearing one, because a warm cache
// means construction compiled nothing at all and the session being recreated here never existed.
//
// The sessions are the GPU context's and every owner shares them: between the renderer's drop
// and its next compile a second owner compiles through them and drops them again, and the
// renderer's kernel still builds.
TEST_CASE(
	"A compute kernel built after device creation recreates the Slang session",
	"[compute][device]")
{
	auto ctxDesc                     = bgpu::GpuContextDesc();
	ctxDesc.enableDebugLayer         = true;
	ctxDesc.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

	ctxDesc.shaderCacheDir = bgl::test::ShaderCacheDir();

	// This case owns its context, so the suite's is let go first: one is live per process.
	bgl::test::ReleaseGpuContext();

	for (int pass = 0; pass < 2; ++pass)
	{
		CAPTURE(pass);

		auto context = bgpu::CreateGpuContext(ctxDesc);
		auto gfx     = bgl::CreateGraphics(context, bgl::GraphicsOptions());
		REQUIRE(gfx != nullptr);

		// The second owner.
		REQUIRE(context->LoadModule("CSComputeBufferTest") != nullptr);
		context->ReleaseSlangSessions();

		auto gfxBase = gfx->As<bgl::GraphicsBase>();
		REQUIRE(gfxBase != nullptr);

		auto resourceManager = gfxBase->GetResourceManagerCpy();
		REQUIRE(resourceManager != nullptr);

		auto device = gfxBase->GetDevice();

		auto cmdListDesc = bgpu::CommandListDesc();
		cmdListDesc.type = bgpu::QueueType::kGraphics;

		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

		constexpr uint32_t c_Count = 8;

		auto bufDesc = bgpu::ComputeBufferDesc();
		bufDesc.SetElement<uint32_t>().SetInitialCount(c_Count).SetDebugName("Slang Session Out");

		auto outBuf = resourceManager->CreateComputeBuffer(bufDesc);
		REQUIRE(resourceManager->ValidBufferHandle(outBuf));

		auto kernel = device->CreateComputeKernel(
			bgpu::ComputePipelineDesc()
				.SetShader(device->CreateShader("CSComputeBufferTest"))
				.SetDebugName("CSComputeBufferTest"));

		kernel["gUniforms"]["outBuffer"] = outBuf;

		auto state   = bgpu::ComputeState();
		state.kernel = &kernel;

		auto rbDesc      = bgpu::ReadbackBufferDesc();
		rbDesc.byteSize  = c_Count * sizeof(uint32_t);
		rbDesc.debugName = "Slang Session Readback";

		auto rb = resourceManager->CreateReadbackBuffer(rbDesc);

		cmdList->Open(cmdQueue, cmdAllocator);
		cmdList->SetComputeState(state);
		cmdList->Dispatch(1, 1, 1);

		cmdList->Barrier(
			outBuf,
			bgpu::BufferBarrierDesc()
				.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
				.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
				.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
				.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));

		cmdList->CopyBufferToReadback(rb, outBuf);
		cmdList->Close();

		auto fence = cmdQueue->ExecuteCommandList(cmdList);
		cmdQueue->WaitForFenceCPUBlocking(fence);

		const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(rb));
		REQUIRE(mapped != nullptr);

		for (uint32_t i = 0; i < c_Count; ++i)
		{
			CHECK(mapped[i] == i * 10u + 1u);
		}

		resourceManager->UnmapReadback(rb);

		resourceManager->DestroyReadbackBuffer(rb, false);
		resourceManager->DestroyBuffer(outBuf, false);
	}
}
