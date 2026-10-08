#include "gfx/GraphicsBase.h"
#include "util/GpuValidation.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <bgl/IGraphics.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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

	// A look thinning by count from 10 m, eight blades a clump, with a fade too far off to matter:
	// at every distance i * 1 m, the share, the blades launched, the widening, every blade's scale,
	// and the last blade's scale for the same look with thinning off.
	constexpr float    c_ThinStart  = 10.0f;
	constexpr uint32_t c_Blades     = 8;
	constexpr uint32_t c_ThinStride = 4 + c_Blades;
	constexpr uint32_t c_ThinFirst  = 2 * c_Steps + c_Visible;
	constexpr uint32_t c_Count      = c_ThinFirst + c_Steps * c_ThinStride;

	// The scale and the widening at every distance i * 0.5 m, then BladeVisible at four cases, then
	// the thinning look's samples.
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
    look.bladesPerClump = 8u;

    for (uint i = 0u; i < 100u; ++i)
    {
        let thinning = ThinningAt(look, float(i) * 0.5, 0u);
        gUniforms.outBuffer[i] = thinning.scale;
        gUniforms.outBuffer[100u + i] = thinning.widen;
    }

    // A 0.2 m blade at 1000 pixels a unit spans 1 pixel at 200 m.
    gUniforms.outBuffer[200u] = BladeVisible(0.2, 150.0, 1000.0) ? 1.0 : 0.0;
    gUniforms.outBuffer[201u] = BladeVisible(0.2, 199.0, 1000.0) ? 1.0 : 0.0;
    gUniforms.outBuffer[202u] = BladeVisible(0.2, 201.0, 1000.0) ? 1.0 : 0.0;
    gUniforms.outBuffer[203u] = BladeVisible(0.0, 1.0, 1000.0) ? 1.0 : 0.0;

    GrassLook thin = {};
    thin.fadeStart = 1000.0;
    thin.fadeEnd = 2000.0;
    thin.bladesPerClump = 8u;
    thin.thinStart = 10.0;
    GrassLook whole = thin;
    whole.thinStart = 0.0;

    for (uint i = 0u; i < 100u; ++i)
    {
        let distance = float(i);
        let at = 204u + i * 12u;
        gUniforms.outBuffer[at] = ThinShare(thin, distance);
        gUniforms.outBuffer[at + 1u] = float(KeptBladesPerClump(thin, distance));
        gUniforms.outBuffer[at + 2u] = ThinningAt(thin, distance, 0u).widen;
        gUniforms.outBuffer[at + 3u] = ThinningAt(whole, distance, 7u).scale;
        for (uint k = 0u; k < 8u; ++k)
        {
            gUniforms.outBuffer[at + 4u + k] = ThinningAt(thin, distance, k).scale;
        }
    }
}
)";

	std::vector<float>
	Sample()
	{
		auto opts                                = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer         = true;
		opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::test::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);
		auto gfxBase = gfx->As<bgl::GraphicsBase>();
		REQUIRE(gfxBase != nullptr);

		auto resourceManager = gfxBase->GetResourceManagerCpy();
		auto device          = gfxBase->GetDevice();
		device->AddSourceModule({ "CSGrassThinningProbe", std::string(c_Probe), false });

		auto cmdListDesc  = bgpu::CommandListDesc();
		cmdListDesc.type  = bgpu::QueueType::kGraphics;
		auto cmdAllocator = device->CreateCommandAllocator();
		auto cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
		auto cmdQueue     = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

		auto bufDesc = bgpu::ComputeBufferDesc();
		bufDesc.SetElement<float>().SetInitialCount(c_Count).SetDebugName(
			"Grass Thinning Probe Out");
		auto outBuf = resourceManager->CreateComputeBuffer(bufDesc);

		auto kernel = device->CreateComputeKernel(
			bgpu::ComputePipelineDesc()
				.SetShader(device->CreateShader("CSGrassThinningProbe"))
				.SetDebugName("CSGrassThinningProbe"));
		kernel["gUniforms"]["outBuffer"] = outBuf;

		auto state   = bgpu::ComputeState();
		state.kernel = &kernel;

		auto rbDesc      = bgpu::ReadbackBufferDesc();
		rbDesc.byteSize  = c_Count * sizeof(float);
		rbDesc.debugName = "Grass Thinning Probe Readback";
		auto rb          = resourceManager->CreateReadbackBuffer(rbDesc);

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

TEST_CASE(
	"Past its thinning start a clump keeps a falling share of its blades, launched as a prefix",
	"[grass][thinning]")
{
	const std::vector<float> values = Sample();

	for (uint32_t i = 0; i < c_Steps; ++i)
	{
		const float  distance = static_cast<float>(i);
		const float* at       = values.data() + c_ThinFirst + i * c_ThinStride;
		const float  share    = at[0];
		const auto   kept     = static_cast<uint32_t>(at[1]);
		const float  widen    = at[2];
		const float* scale    = at + 4;
		INFO("distance " << distance << " share " << share << " kept " << kept);

		const float ratio = c_ThinStart / distance;
		const float expected =
			distance <= c_ThinStart ? 1.0f : std::max(ratio * ratio, 1.0f / c_Blades);
		CHECK(share == Catch::Approx(expected));
		CHECK(kept == static_cast<uint32_t>(std::ceil(c_Blades * share - 1e-4f)));
		CHECK(widen == Catch::Approx(1.0f / share));

		// Thinning off keeps every blade whole, the last one included.
		CHECK(at[3] == 1.0f);

		// The first blade of a clump is never thinned, and every blade past the launched prefix is
		// already gone, so launching only the prefix draws exactly what thinning keeps.
		CHECK(scale[0] == 1.0f);
		float covered = 0.0f;
		for (uint32_t k = 0; k < c_Blades; ++k)
		{
			if (k > 0)
				CHECK(scale[k] <= scale[k - 1]);
			if (k >= kept)
				CHECK(scale[k] == 0.0f);
			covered += scale[k];
		}

		// The blades left, widened, cover the ground the whole clump did.
		CHECK(covered * widen == Catch::Approx(static_cast<float>(c_Blades)).epsilon(1e-4));

		// A blade leaves by shrinking: between two metres none falls by more than the share does.
		if (i > 0)
		{
			const float* before    = values.data() + c_ThinFirst + (i - 1) * c_ThinStride;
			const float  shareStep = c_Blades * (before[0] - share);
			for (uint32_t k = 0; k < c_Blades; ++k)
			{
				CHECK(scale[k] <= before[4 + k]);
				CHECK(before[4 + k] - scale[k] <= shareStep + 1e-5f);
			}
		}
	}
}
