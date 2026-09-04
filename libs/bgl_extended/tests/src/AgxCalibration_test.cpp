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
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

namespace
{
	// The sRGB transfer function the backbuffer applies on write. AgX linearizes its own
	// display-encoded result so this re-encode is the last step, and it is where the number a person
	// would sample off a screenshot finally appears.
	float
	EncodeSrgb(float linear)
	{
		return linear <= 0.0031308f ? linear * 12.92f :
		                              1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
	}

	/** AgX(grey) as the shipped tone map computes it, in scene-linear output. */
	glm::vec4
	RunAgX(float sceneLinear)
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;

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

/**
 * Middle grey comes out where Blender's AgX puts it.
 *
 * This is the anchor every comparison against another renderer is made through, and it is exactly
 * the kind of constant that drifts silently: the polynomial, the log range and the two matrices can
 * each be edited into something that still looks like a tone map. Scene-linear 0.18 landing at
 * display 0.5 is the one number that says they are all still right together.
 *
 * Also catches the commonest way to break AgX -- dropping the closing linearization, which leaves
 * the sRGB target encoding an already-encoded value and washes the whole image out. That mistake
 * moves this to about 0.73.
 */
TEST_CASE("AgX places middle grey at display 0.5", "[tonemap][agx][calibration]")
{
	const glm::vec4 result = RunAgX(0.18f);

	// Grey in, grey out: the inset/outset pair is its own inverse on the neutral axis, so a
	// transposed matrix shows up here as a tint before it shows up as a level.
	CHECK(result.r == Catch::Approx(result.g).margin(0.005));
	CHECK(result.g == Catch::Approx(result.b).margin(0.005));

	const float display = EncodeSrgb(result.r);
	INFO("AgX(0.18) = " << result.r << " linear, " << display << " display");
	CHECK(display == Catch::Approx(0.5f).margin(0.01));
}

// The curve still has to be a curve. A tone map that had collapsed to a constant would satisfy the
// anchor above and nothing else, and flatness is the symptom this whole area is being measured for.
TEST_CASE("AgX keeps its range monotonic around middle grey", "[tonemap][agx][calibration]")
{
	const float dark   = EncodeSrgb(RunAgX(0.045f).r);  // two stops under
	const float middle = EncodeSrgb(RunAgX(0.18f).r);
	const float bright = EncodeSrgb(RunAgX(0.72f).r);  // two stops over

	INFO("dark " << dark << ", middle " << middle << ", bright " << bright);

	CHECK(dark < middle);
	CHECK(middle < bright);

	// Two stops either side of grey must still be plainly apart after the compression, or a
	// four-stop environment would read as one flat tone.
	CHECK(middle - dark > 0.1f);
	CHECK(bright - middle > 0.1f);
}
