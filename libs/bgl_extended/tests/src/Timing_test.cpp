#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "cmd/TimestampHeap.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "types/Barrier.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "util/GpuValidation.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/ImageData.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/PassTiming.h>
#include <bgl/RenderJob.h>
#include <bgl/SkyboxDesc.h>
#include <bgl/Viewport.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// The RHI's timestamp query, below the frame graph: a span around a dispatch reads back a start
// before its end, and a span around nothing the GPU can sample says so rather than reporting a
// stale pair. Backend-agnostic -- D3D12 writes a query heap, Metal attaches a counter sample
// buffer to the encoder the dispatch opens.
TEST_CASE("A timed span brackets the work recorded inside it", "[timing]")
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
	auto device          = gfxBase->GetDevice();

	auto heap = device->CreateTimestampHeap(8);
	if (heap == nullptr)
	{
		SKIP("The device cannot sample a timestamp at a pass boundary");
	}
	REQUIRE(heap->GetCapacity() == 8);

	auto cmdListDesc = bgl::CommandListDesc();
	cmdListDesc.type = bgl::QueueType::kGraphics;

	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	// Ticks per second is what turns a pair of slots into a duration, and it is the one number
	// here that a backend could plausibly report as unknown.
	const double frequency = cmdQueue->GetTimestampFrequency();
	CHECK(frequency > 0.0);

	auto bufDesc = bgl::ComputeBufferDesc();
	bufDesc.SetElement<uint32_t>().SetInitialCount(8).SetDebugName("Timing Out Buffer");
	auto outBuf = resourceManager->CreateComputeBuffer(bufDesc);

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSComputeBufferTest"))
			.SetDebugName("CSComputeBufferTest"));
	kernel["gUniforms"]["outBuffer"] = outBuf;

	auto state   = bgl::ComputeState();
	state.kernel = &kernel;

	cmdList->Open(cmdQueue, cmdAllocator);

	// Slots 0-1: a span with a dispatch in it.
	cmdList->BeginTiming(*heap, 0, 1);
	cmdList->SetComputeState(state);
	cmdList->Dispatch(1, 1, 1);
	const bool dispatchSampled = cmdList->EndTiming();

	// Slots 2-3: a span holding a barrier and nothing else. D3D12 samples it regardless; Metal has
	// no encoder to hang a sample on and must say so.
	cmdList->BeginTiming(*heap, 2, 3);
	cmdList->Barrier(
		outBuf,
		bgl::BufferBarrierDesc()
			.AddSyncBefore(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgl::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgl::BarrierSyncFlag::kComputeShader)
			.AddAccessAfter(bgl::BarrierAccessFlag::kUnorderedAccess));
	const bool barrierSampled = cmdList->EndTiming();

	cmdList->ResolveTimestamps(*heap, 0, 4);
	cmdList->Close();

	cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

	std::array<uint64_t, 4> ticks{};
	heap->Read(0, ticks);

	REQUIRE(dispatchSampled);
	CHECK(ticks[0] != bgl::ITimestampHeap::c_UnwrittenTimestamp);
	CHECK(ticks[1] != bgl::ITimestampHeap::c_UnwrittenTimestamp);
	CHECK(ticks[1] >= ticks[0]);

	if (barrierSampled)
	{
		CHECK(ticks[2] != bgl::ITimestampHeap::c_UnwrittenTimestamp);
		CHECK(ticks[3] >= ticks[2]);
	}
	else
	{
		CHECK(ticks[2] == bgl::ITimestampHeap::c_UnwrittenTimestamp);
		CHECK(ticks[3] == bgl::ITimestampHeap::c_UnwrittenTimestamp);
	}

	// A dispatch of one threadgroup is microseconds, not a frame: a duration past a second means
	// the ticks were not converted, or the two slots came from different work.
	const double seconds = static_cast<double>(ticks[1] - ticks[0]) / frequency;
	CHECK(seconds < 1.0);

	// A span whose slots were never sampled must not leave a stale pair behind either.
	std::array<uint64_t, 2> untouched{};
	heap->Read(6, untouched);
	CHECK(untouched[0] == bgl::ITimestampHeap::c_UnwrittenTimestamp);
	CHECK(untouched[1] == bgl::ITimestampHeap::c_UnwrittenTimestamp);

	cmdQueue->Flush();
	resourceManager->DestroyBuffer(outBuf, false);
}

namespace
{
	struct TimedScene
	{
		bgl::GraphicsRef     gfx;
		bgl::RenderTargetRef target;
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
		bgl::RenderJob       job;

		static constexpr uint32_t c_Width  = 160;
		static constexpr uint32_t c_Height = 120;

		explicit TimedScene(bool taa = true)
		{
			auto opts             = bgl::GraphicsOptions();
			opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
			opts.enableDebugLayer = true;
			gfx                   = bgl::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			auto targetDesc       = bgl::RenderTargetDesc();
			targetDesc.width      = static_cast<int>(c_Width);
			targetDesc.height     = static_cast<int>(c_Height);
			targetDesc.headless   = true;
			targetDesc.taaEnabled = taa;
			target                = gfx->CreateRenderTarget(targetDesc);

			auto sceneDesc                        = bgl::SceneDesc();
			sceneDesc.initialGeom                 = 4;
			sceneDesc.initialMeshlets             = 256;
			sceneDesc.initialSubmeshes            = 4;
			sceneDesc.initialVertexBufferByteSize = 400000;
			sceneDesc.initialIndices              = 10000;
			sceneDesc.initialPbrMaterials         = 4;
			scene                                 = gfx->CreateScene(sceneDesc);
			view                                  = gfx->CreateSceneView(scene, 4);

			auto cubeTex = bgl::test::LoadSkybox(scene.Get());
			view->SetSkyBox(bgl::SkyboxDesc{ cubeTex });

			auto geom = scene->AddCubeGeom();
			(void)view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));

			auto camera = bgl::Camera();
			camera
				.LookAt(
					glm::vec3(0.0f, 0.0f, 5.0f),
					glm::vec3(0.0f, 0.0f, 4.0f),
					glm::vec3(0.0f, 1.0f, 0.0f))
				.Perspective(
					glm::radians(60.0f),
					static_cast<float>(c_Width) / static_cast<float>(c_Height),
					0.5f,
					500.0f);

			job.view     = view;
			job.camera   = camera;
			job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));
		}

		[[nodiscard]] bool
		CanTime() const
		{
			// A device that cannot sample at a pass boundary has no heap, and arms nothing.
			auto* gfxBase = gfx->As<bgl::GraphicsBase>();
			return gfxBase->GetDevice()->CreateTimestampHeap(2) != nullptr;
		}
	};

	[[nodiscard]] std::vector<std::string>
	Names(const std::vector<bgl::PassTiming>& rows)
	{
		std::vector<std::string> names;
		names.reserve(rows.size());
		for (const bgl::PassTiming& row : rows)
		{
			names.push_back(row.name);
		}
		return names;
	}
}

// The rows are the frame's passes in the order the graph ran them, each with a duration the GPU
// measured; the whole frame cannot have taken longer than the CPU spent waiting for it, which is
// what catches a tick rate converted wrongly. The pass names pinned here are the ones every frame
// carries: a rename there is a rename in the breakdown a person reads.
TEST_CASE("Timing a frame lists every kept pass with what it cost", "[timing][render]")
{
	TimedScene s;
	if (!s.CanTime())
	{
		SKIP("The device cannot sample a timestamp at a pass boundary");
	}

	s.target->SetGpuTimingEnabled(true);

	const auto cpuStart = std::chrono::steady_clock::now();
	s.gfx->DrawFrame(s.target, s.job);
	s.gfx->WaitIdle();
	const double cpuMs =
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuStart)
			.count();

	const std::vector<bgl::PassTiming> rows = s.gfx->GetPassTimings(s.target);
	REQUIRE(!rows.empty());

	const std::vector<std::string> names = Names(rows);
	CHECK(names.front() == "Clear");
	CHECK(names.back() == "PreparePresent");
	CHECK(std::ranges::find(names, "Forward 0") != names.end());
	CHECK(std::ranges::find(names, "TaaResolve") != names.end());
	CHECK(std::ranges::find(names, "PostProcess") != names.end());

	// In execution order, so Forward precedes the resolve that reads what it drew.
	const auto forward = std::ranges::find(names, "Forward 0");
	const auto resolve = std::ranges::find(names, "TaaResolve");
	CHECK(forward < resolve);

	double sumMs = 0.0;
	for (const bgl::PassTiming& row : rows)
	{
		CHECK(row.milliseconds >= 0.0);
		sumMs += row.milliseconds;
	}
	CHECK(sumMs > 0.0);
	CHECK(sumMs <= cpuMs);
}

// Off is the default, and turning it off again forgets the rows: a reader that did not ask to
// time a frame is told nothing rather than about the last one that was.
TEST_CASE("Timing off reports no rows", "[timing][render]")
{
	TimedScene s;
	if (!s.CanTime())
	{
		SKIP("The device cannot sample a timestamp at a pass boundary");
	}

	CHECK(!s.target->IsGpuTimingEnabled());
	s.gfx->DrawFrame(s.target, s.job);
	s.gfx->WaitIdle();
	CHECK(s.gfx->GetPassTimings(s.target).empty());

	s.target->SetGpuTimingEnabled(true);
	s.gfx->DrawFrame(s.target, s.job);
	s.gfx->WaitIdle();
	CHECK(!s.gfx->GetPassTimings(s.target).empty());

	s.target->SetGpuTimingEnabled(false);
	CHECK(s.gfx->GetPassTimings(s.target).empty());

	// A frame drawn while off arms nothing, so the rows stay empty rather than reviving.
	s.gfx->DrawFrame(s.target, s.job);
	s.gfx->WaitIdle();
	CHECK(s.gfx->GetPassTimings(s.target).empty());
}

// The rows trail the frame by the fence, not by a fixed count: a frame timed and then waited on is
// readable at once, and one queued behind another lands with it.
TEST_CASE("Timing switched on mid-run reports rows within two frames", "[timing][render]")
{
	TimedScene s;
	if (!s.CanTime())
	{
		SKIP("The device cannot sample a timestamp at a pass boundary");
	}

	s.gfx->DrawFrame(s.target, s.job);
	s.gfx->DrawFrame(s.target, s.job);
	s.target->SetGpuTimingEnabled(true);

	s.gfx->DrawFrame(s.target, s.job);
	s.gfx->DrawFrame(s.target, s.job);
	s.gfx->WaitIdle();
	CHECK(!s.gfx->GetPassTimings(s.target).empty());
}

// On Metal a timed pass ends its encoder, which is a tile store and reload the untimed frame never
// does; the point is that the image does not know. Without TAA a still scene draws the same pixels
// every frame -- no jitter walks, no hash seed advances -- so any difference between an untimed
// frame and a timed one would be the split showing.
TEST_CASE("Timing a frame changes nothing a pixel can see", "[timing][render]")
{
	TimedScene s(false);
	if (!s.CanTime())
	{
		SKIP("The device cannot sample a timestamp at a pass boundary");
	}

	const auto capture = [&](bool timed) {
		s.target->SetGpuTimingEnabled(timed);
		s.gfx->DrawFrame(s.target, s.job);
		return s.gfx->ScreenshotToMemory(s.target);
	};

	const auto same = [](const assetlib::ImageData& a, const assetlib::ImageData& b) {
		return a.pixels.size() == b.pixels.size() &&
		       std::equal(a.pixels.begin(), a.pixels.end(), b.pixels.begin());
	};

	// The premise first: two untimed frames repeat, or the comparison below means nothing.
	const assetlib::ImageData first  = capture(false);
	const assetlib::ImageData second = capture(false);
	REQUIRE(same(first, second));

	const assetlib::ImageData timed = capture(true);
	CHECK(same(first, timed));
}
