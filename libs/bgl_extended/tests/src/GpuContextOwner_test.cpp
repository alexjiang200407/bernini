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
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <bgl/glm.h>
#include <bgl/types/SceneDesc.h>
#include <bgpu/GpuContext.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <string>

// The renderer as one owner of a GPU context among several. What these cases pin is the shape
// the crowd simulation is built on: a context the application made, a renderer that borrows it, and
// room beside the renderer for another owner of the same device.

namespace
{
	// These cases own their context, so the suite's is let go first: one is live per process.
	bgpu::GpuContextRef
	MakeContext(const std::filesystem::path& clientShaderDir = {})
	{
		bgl::test::ReleaseGpuContext();

		auto desc                     = bgpu::GpuContextDesc();
		desc.enableDebugLayer         = true;
		desc.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();
		desc.clientShaderDir          = clientShaderDir;
		desc.shaderCacheDir           = bgl::test::ShaderCacheDir();
		return bgpu::CreateGpuContext(desc);
	}

	// Builds CSSourceProbe on the renderer's device, dispatches it once and returns the uint it wrote:
	// whatever `game.probe` says in this context's sessions.
	uint32_t
	ReadProbe(const bgl::GraphicsRef& gfx)
	{
		auto* gfxBase = gfx->As<bgl::GraphicsBase>();
		REQUIRE(gfxBase != nullptr);

		auto  resourceManager = gfxBase->GetResourceManagerCpy();
		auto* device          = gfxBase->GetDevice();

		auto cmdListDesc = bgl::CommandListDesc();
		cmdListDesc.type = bgl::QueueType::kGraphics;

		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

		auto bufDesc = bgl::ComputeBufferDesc();
		bufDesc.SetElement<uint32_t>().SetInitialCount(1).SetDebugName("Owner Probe Out");
		auto outBuf = resourceManager->CreateComputeBuffer(bufDesc);

		auto kernel = device->CreateComputeKernel(
			bgl::ComputePipelineDesc()
				.SetShader(device->CreateShader("CSSourceProbe"))
				.SetDebugName("CSSourceProbe"));
		kernel["gUniforms"]["outBuffer"] = outBuf;

		auto state   = bgl::ComputeState();
		state.kernel = &kernel;

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = sizeof(uint32_t);
		rbDesc.debugName = "Owner Probe Readback";
		auto rb          = resourceManager->CreateReadbackBuffer(rbDesc);

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

		cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

		const auto* mapped = static_cast<const uint32_t*>(resourceManager->MapReadback(rb));
		REQUIRE(mapped != nullptr);
		const uint32_t value = mapped[0];
		resourceManager->UnmapReadback(rb);

		resourceManager->DestroyReadbackBuffer(rb, false);
		resourceManager->DestroyBuffer(outBuf, false);
		return value;
	}

	// One headless frame of an empty scene: enough to put the renderer's queue, allocator and
	// resource manager through a submission on the shared device.
	void
	DrawOneFrame(const bgl::GraphicsRef& gfx)
	{
		constexpr uint32_t c_Size = 64;

		auto desc     = bgl::RenderTargetDesc();
		desc.width    = c_Size;
		desc.height   = c_Size;
		desc.headless = true;
		auto target   = gfx->CreateRenderTarget(desc);

		auto scene = gfx->CreateScene(bgl::SceneDesc());
		auto view  = gfx->CreateSceneView(scene, 8);

		auto camera = bgl::Camera();
		camera.LookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));

		gfx->DrawFrame(target, job);
		gfx->WaitIdle();
	}
}

TEST_CASE("A GPU context outlives the renderer built on it", "[device]")
{
	auto context = MakeContext();

	{
		auto gfx = bgl::CreateGraphics(context, bgl::GraphicsOptions());
		REQUIRE(gfx != nullptr);
		DrawOneFrame(gfx);
	}

	// The renderer drained and released everything it made; the device and the compiler stand.
	CHECK(context->LoadModule("CSComputeBufferTest") != nullptr);

	auto again = bgl::CreateGraphics(context, bgl::GraphicsOptions());
	REQUIRE(again != nullptr);
	DrawOneFrame(again);
}

// Two owners at once, each with its own queue and pools on the one device. Two renderers stand in
// for a renderer and a compute client: the isolation they need is the same.
TEST_CASE("Two renderers share one GPU context at once", "[device]")
{
	auto context = MakeContext();

	auto first  = bgl::CreateGraphics(context, bgl::GraphicsOptions());
	auto second = bgl::CreateGraphics(context, bgl::GraphicsOptions());
	REQUIRE(first != nullptr);
	REQUIRE(second != nullptr);

	DrawOneFrame(first);
	DrawOneFrame(second);

	SECTION("the first owner goes first")
	{
		first = nullptr;
		DrawOneFrame(second);
	}

	SECTION("the second owner goes first")
	{
		second = nullptr;
		DrawOneFrame(first);
	}
}

// The renderer holds its context: a caller that drops its own reference right after CreateGraphics
// has not pulled the device out from under the frame.
TEST_CASE("A renderer keeps its GPU context alive", "[device]")
{
	auto gfx = bgl::CreateGraphics(MakeContext(), bgl::GraphicsOptions());
	REQUIRE(gfx != nullptr);
	DrawOneFrame(gfx);
}

// Registration is per renderer and the sessions are per context, so the second renderer on a context
// with surfaces binds every slot and program the first already did. A name maps to one text: the
// second registration is no registration, not a second copy of every module.
TEST_CASE("Two renderers on one context bind the same surfaces once", "[device]")
{
	auto context = MakeContext("./shaders/tests/surfaces");

	auto first = bgl::CreateGraphics(context, bgl::GraphicsOptions());
	REQUIRE(first != nullptr);
	const auto surfaces = first->GetSurfaceTypes().size();
	REQUIRE(surfaces > 0);

	auto second = bgl::CreateGraphics(context, bgl::GraphicsOptions());
	REQUIRE(second != nullptr);
	CHECK(second->GetSurfaceTypes().size() == surfaces);

	DrawOneFrame(first);
	DrawOneFrame(second);
}

// The rule the case above rests on, from the RHI: a module registered again under its name replaces
// the text every later compile reads, and the cache salt follows it.
TEST_CASE("A source module registered again under its name replaces the text", "[device][compute]")
{
	auto context = MakeContext();
	auto gfx     = bgl::CreateGraphics(context, bgl::GraphicsOptions());
	REQUIRE(gfx != nullptr);

	auto* device = gfx->As<bgl::GraphicsBase>()->GetDevice();

	device->AddSourceModule({ "game.probe", "public static const uint kProbeValue = 2u;\n" });
	CHECK(ReadProbe(gfx) == 2u);

	device->AddSourceModule({ "game.probe", "public static const uint kProbeValue = 5u;\n" });
	CHECK(ReadProbe(gfx) == 5u);

	// The same text again is no change: the sessions and their loaded modules stand.
	device->AddSourceModule({ "game.probe", "public static const uint kProbeValue = 5u;\n" });
	CHECK(ReadProbe(gfx) == 5u);
}
