#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
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
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// How a field fades with distance, read straight off lib.forward.grass: a probe kernel samples
// ThinningAt over a run of distances and BladeVisible about its threshold. The grass stage sizes and
// keeps blades by exactly these, so what they return is what a fading field draws; which chunks a
// view reaches is the culling suite's.

namespace
{
	constexpr uint32_t c_Steps     = 100;
	constexpr float    c_Step      = 0.5f;  // metres between samples
	constexpr float    c_FadeStart = 10.0f;
	constexpr float    c_FadeEnd   = 30.0f;
	constexpr float    c_Widening  = 1.0f;
	constexpr uint32_t c_Visible   = 4;
	constexpr uint32_t c_Count     = 2 * c_Steps + c_Visible;

	// The scale and the widening at every distance i * 0.5 m, then BladeVisible at four cases.
	constexpr std::string_view c_Probe = R"(import lib.forward.grass;
import idl.GrassLook;
import lib.types.ComputeBuffer;

struct Uniforms
{
    ComputeBuffer<float> outBuffer;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    GrassLook look = {};
    look.fadeStart = 10.0;
    look.fadeEnd = 30.0;
    look.widening = 1.0;

    for (uint i = 0u; i < 100u; ++i)
    {
        let thinning = ThinningAt(look, float(i) * 0.5);
        gUniforms.outBuffer[i] = thinning.scale;
        gUniforms.outBuffer[100u + i] = thinning.widen;
    }

    // A 0.2 m blade at 1000 pixels a unit spans 1 pixel at 200 m.
    gUniforms.outBuffer[200u] = BladeVisible(0.2, 150.0, 1000.0) ? 1.0 : 0.0;
    gUniforms.outBuffer[201u] = BladeVisible(0.2, 199.0, 1000.0) ? 1.0 : 0.0;
    gUniforms.outBuffer[202u] = BladeVisible(0.2, 201.0, 1000.0) ? 1.0 : 0.0;
    gUniforms.outBuffer[203u] = BladeVisible(0.0, 1.0, 1000.0) ? 1.0 : 0.0;
}
)";

	std::vector<float>
	Sample()
	{
		auto opts                             = bgl::test::GraphicsSetup();
		opts.graphics.shaderCacheDir          = bgl::test::ShaderCacheDir();
		opts.context.enableDebugLayer         = true;
		opts.context.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::test::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);
		auto gfxBase = gfx->As<bgl::GraphicsBase>();
		REQUIRE(gfxBase != nullptr);

		auto resourceManager = gfxBase->GetResourceManagerCpy();
		auto device          = gfxBase->GetDevice();
		device->AddSourceModule({ "CSGrassThinningProbe", std::string(c_Probe), false });

		auto cmdListDesc  = bgl::CommandListDesc();
		cmdListDesc.type  = bgl::QueueType::kGraphics;
		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);

		auto bufDesc = bgl::ComputeBufferDesc();
		bufDesc.SetElement<float>().SetInitialCount(c_Count).SetDebugName(
			"Grass Thinning Probe Out");
		auto outBuf = resourceManager->CreateComputeBuffer(bufDesc);

		auto kernel = device->CreateComputeKernel(
			bgl::ComputePipelineDesc()
				.SetShader(device->CreateShader("CSGrassThinningProbe"))
				.SetDebugName("CSGrassThinningProbe"));
		kernel["gUniforms"]["outBuffer"] = outBuf;

		auto state   = bgl::ComputeState();
		state.kernel = &kernel;

		auto rbDesc      = bgl::ReadbackBufferDesc();
		rbDesc.byteSize  = c_Count * sizeof(float);
		rbDesc.debugName = "Grass Thinning Probe Readback";
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

		const auto* mapped = static_cast<const float*>(resourceManager->MapReadback(rb));
		REQUIRE(mapped != nullptr);
		auto values = std::vector<float>(mapped, mapped + c_Count);
		resourceManager->UnmapReadback(rb);

		resourceManager->DestroyReadbackBuffer(rb, false);
		resourceManager->DestroyBuffer(outBuf, false);
		return values;
	}
}

TEST_CASE(
	"A field fades by shrinking together, and drops only blades too small to see",
	"[grass][thinning]")
{
	const std::vector<float> values = Sample();
	const float*             scale  = values.data();
	const float*             widen  = values.data() + c_Steps;

	// The steepest the fade may be between two samples: a blade moving by one step changes size by
	// at most that, never from whole to nothing, and every blade at one distance is the same size.
	const float slope = c_Step / (c_FadeEnd - c_FadeStart);
	for (uint32_t i = 0; i < c_Steps; ++i)
	{
		const float distance = static_cast<float>(i) * c_Step;
		INFO("distance " << distance << " scale " << scale[i] << " widen " << widen[i]);
		if (distance <= c_FadeStart)
			CHECK(scale[i] == 1.0f);
		if (distance >= c_FadeEnd)
			CHECK(scale[i] == 0.0f);
		CHECK(widen[i] == Catch::Approx(1.0f + c_Widening * (1.0f - scale[i])));
		if (i > 0)
		{
			CHECK(scale[i] <= scale[i - 1]);
			CHECK(scale[i - 1] - scale[i] <= slope + 1e-5f);
		}
	}

	const float* visible = values.data() + 2 * c_Steps;
	CHECK(visible[0] == 1.0f);
	CHECK(visible[1] == 1.0f);
	CHECK(visible[2] == 0.0f);
	CHECK(visible[3] == 0.0f);
}
