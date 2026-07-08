#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "debug/DebugBuffer.h"
#include "debug/DebugReadback.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"
#include <bgl/IGpuAssertionHandler.h>
#include <bgl/IGraphics.h>
#include <bgl/MaterialType.h>

#if defined(BERNINI_GPU_DEBUG)

namespace
{
	// Builds a raw debug-buffer word image (header + packed records) the way the GPU
	// would leave it, for exercising the decoder without a device.
	std::vector<uint32_t>
	MakeDebugImage(uint32_t count, uint32_t overflow, uint32_t capacity)
	{
		std::vector<uint32_t> words(
			bgl::DebugBuffer::kHeaderWords + capacity * bgl::DebugBuffer::kRecordWords,
			0u);
		words[bgl::DebugBuffer::kCounterWord]  = count;
		words[bgl::DebugBuffer::kOverflowWord] = overflow;
		words[bgl::DebugBuffer::kCapacityWord] = capacity;
		return words;
	}

	void
	SetRecord(std::vector<uint32_t>& words, uint32_t i, uint32_t errcode)
	{
		const uint32_t base = bgl::DebugBuffer::kHeaderWords + i * bgl::DebugBuffer::kRecordWords;
		words[base]         = errcode;
	}
}

// The decoder is pure, so its contract (including the crash-trigger condition) can be
// verified without a GPU or terminating the process.
TEST_CASE("InspectDebugReadback decodes debug-buffer records", "[debug][gpu-assert]")
{
	SECTION("no assertion -> nullopt")
	{
		const auto image  = MakeDebugImage(/*count*/ 0, /*overflow*/ 0, /*capacity*/ 8);
		const auto report = bgl::InspectDebugReadback(image.data(), 8);
		CHECK_FALSE(report.has_value());
	}

	SECTION("records decode in order")
	{
		auto image = MakeDebugImage(/*count*/ 2, /*overflow*/ 0, /*capacity*/ 8);
		SetRecord(image, 0, 5u);
		SetRecord(image, 1, 6u);

		const auto report = bgl::InspectDebugReadback(image.data(), 8);
		REQUIRE(report.has_value());
		CHECK(report->count == 2);
		CHECK_FALSE(report->overflow);
		REQUIRE(report->records.size() == 2);
		CHECK(report->records[0].errcode == 5u);
		CHECK(report->records[1].errcode == 6u);
	}

	SECTION("overflow caps decoded records at capacity")
	{
		auto image = MakeDebugImage(/*count*/ 20, /*overflow*/ 1, /*capacity*/ 2);
		SetRecord(image, 0, 1u);
		SetRecord(image, 1, 2u);

		const auto report = bgl::InspectDebugReadback(image.data(), 2);
		REQUIRE(report.has_value());
		CHECK(report->count == 20);
		CHECK(report->overflow);
		CHECK(report->records.size() == 2);  // capped, not 20
	}
}

// End-to-end: a compute shader calls dbg_raise() from one thread; the record must
// survive the atomic append, the implicit gDebug auto-bind, and CPU readback.
TEST_CASE("dbg_raise records a GPU assertion end-to-end", "[debug][gpu-assert][compute]")
{
	constexpr uint32_t kCapacity = 16;

	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

	auto resourceManager = gfxBase->GetResourceManagerCpy();
	REQUIRE(resourceManager != nullptr);

	auto device = gfxBase->GetDevice();

	auto cmdListDesc  = bgl::CommandListDesc();
	cmdListDesc.type  = bgl::QueueType::kGraphics;
	auto cmdAllocator = device->CreateCommandAllocator();
	auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
	auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

	auto debugBuffer = bgl::DebugBuffer();
	debugBuffer.Init(kCapacity, resourceManager);

	auto kernel = device->CreateComputeKernel(
		bgl::ComputePipelineDesc()
			.SetShader(device->CreateShader("CSDbgRaiseTest"))
			.SetDebugName("Dbg Raise Test"));
	REQUIRE(kernel.pipeline != nullptr);

	// The shader references the implicit gDebug cbuffer; reflection must surface it so
	// the engine can auto-bind the debug buffer.
	REQUIRE(kernel.uniforms.contains("gDebug"));

	auto rbDesc      = bgl::ReadbackBufferDesc();
	rbDesc.byteSize  = debugBuffer.ByteSize();
	rbDesc.debugName = "Dbg Raise Readback";
	auto rb          = resourceManager->CreateReadbackBuffer(rbDesc);

	const auto bufferBarrier = [](bgl::BarrierSyncFlag   syncBefore,
	                              bgl::BarrierAccessFlag accessBefore,
	                              bgl::BarrierSyncFlag   syncAfter,
	                              bgl::BarrierAccessFlag accessAfter) {
		return bgl::BufferBarrierDesc()
		    .AddSyncBefore(syncBefore)
		    .AddAccessBefore(accessBefore)
		    .AddSyncAfter(syncAfter)
		    .AddAccessAfter(accessAfter);
	};

	cmdList->Open(cmdQueue, cmdAllocator);

	// Reset the header (counter/overflow/capacity), then hand the buffer to the shader.
	debugBuffer.Reset(cmdList);
	cmdList->Barrier(
		debugBuffer.GetBufferHandle(),
		bufferBarrier(
			bgl::BarrierSyncFlag::kCopy,
			bgl::BarrierAccessFlag::kCopyDest,
			bgl::BarrierSyncFlag::kComputeShader,
			bgl::BarrierAccessFlag::kUnorderedAccess));

	auto computeState   = bgl::ComputeState();
	computeState.kernel = &kernel;
	cmdList->SetComputeState(computeState);
	cmdList->SetActiveDebugBuffer(debugBuffer.GetBufferHandle());
	cmdList->Dispatch(1, 1, 1);  // 64 threads; only thread 7 raises.

	cmdList->Barrier(
		debugBuffer.GetBufferHandle(),
		bufferBarrier(
			bgl::BarrierSyncFlag::kComputeShader,
			bgl::BarrierAccessFlag::kUnorderedAccess,
			bgl::BarrierSyncFlag::kCopy,
			bgl::BarrierAccessFlag::kCopySource));
	cmdList->CopyBufferToReadback(rb, debugBuffer.GetBufferHandle());

	cmdList->Close();

	auto fence = cmdQueue->ExecuteCommandList(cmdList);
	cmdQueue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = resourceManager->MapReadback(rb);
	REQUIRE(mapped != nullptr);

	const auto report = bgl::InspectDebugReadback(mapped, kCapacity);
	REQUIRE(report.has_value());
	CHECK(report->count == 1);
	CHECK_FALSE(report->overflow);
	REQUIRE(report->records.size() == 1);
	CHECK(report->records[0].errcode == 1u);

	resourceManager->UnmapReadback(rb);

	debugBuffer.Release(fence, false);
	resourceManager->DestroyReadbackBuffer(rb, fence, false);
}

namespace
{
	// Captures the report instead of crashing, so the handler path can be asserted.
	struct RecordingAssertionHandler : public bgl::IGpuAssertionHandler
	{
		int                   calls       = 0;
		uint32_t              raisedCount = 0;
		std::vector<uint32_t> errcodes;

		void
		OnGpuAssertion(const bgl::GpuAssertionReport& report) noexcept override
		{
			++calls;
			raisedCount = report.raisedCount;
			errcodes.assign(report.errcodes, report.errcodes + report.errcodeCount);
		}
	};
}

// End-to-end through Graphics: a kAssert material raises in the pixel shader; with a
// handler registered the engine must invoke it (with the decoded report) rather than
// crash. The assertion is read back a few frames after it fires, so we pump frames.
TEST_CASE("GPU assertion handler replaces the crash", "[debug][gpu-assert][render]")
{
	auto opts                     = bgl::GraphicsOptions();
	opts.enableDebugLayer         = true;
	opts.enableGPUValidationLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	RecordingAssertionHandler handler;
	gfx->SetGpuAssertionHandler(&handler);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 64;
	targetDesc.height   = 64;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 8;
	sceneDesc.maxMeshlets             = 512;
	sceneDesc.maxSubmeshes            = 8;
	sceneDesc.maxVertexBufferByteSize = 800000;
	sceneDesc.maxIndices              = 20000;

	auto scene          = gfx->CreateScene(sceneDesc);
	auto view           = gfx->CreateSceneView(scene, 8);
	auto assertMaterial = bgl::MaterialHandle(bgl::MaterialType::kAssert, core::slot_handle());
	auto cube           = scene->AddCubeGeom(assertMaterial);
	view->CreateStaticMeshInstance(cube, glm::mat4(1.0f));

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 20.0f),
			glm::vec3(0.0f, 0.0f, 19.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);

	auto context     = bgl::RenderContext();
	context.view     = view;
	context.camera   = camera;
	context.viewport = bgl::Viewport(64.0f, 64.0f);

	// The snapshot is inspected a few frames after it is recorded (readback ring), so
	// pump frames until the handler fires (bounded so a real failure still terminates).
	for (int i = 0; i < 8 && handler.calls == 0; ++i)
	{
		gfx->DrawFrame(target, context);
	}

	CHECK(handler.calls >= 1);
	CHECK(handler.raisedCount >= 1);
	REQUIRE(handler.errcodes.size() >= 1);
	CHECK(handler.errcodes[0] == 1u);  // Forward_Assert.slang raises errcode 1

	gfx->DiscardPendingGpuAssertions();
	gfx->SetGpuAssertionHandler(nullptr);
}

#endif  // BERNINI_GPU_DEBUG
