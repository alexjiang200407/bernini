#include "util/AgxProbe.h"
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline.h"
#include "postprocess/TonemapLut.h"
#include "postprocess/color_grade.h"
#include "resource/Buffer.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "resource/Sampler.h"
#include "types/Barrier.h"
#include "types/ComputeState.h"
#include "types/QueueType.h"
#include "uniforms/Uniforms.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
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

	namespace
	{
		/**
		 * One dispatch of `shader`'s single thread, which writes one float4 to `outColor` after
		 * `bind` has set the rest of `gUniforms`. The LUT is bound as `lut`/`lutSampler`: the same
		 * file and class the renderer samples through, so a probe measures the shipped curve and
		 * not a copy of it.
		 */
		template <typename Bind>
		glm::vec4
		RunProbe(bgl::IGraphics& gfx, const char* shader, const Bind& bind)
		{
			auto* gfxBase = gfx.As<bgl::GraphicsBase>();
			REQUIRE(gfxBase != nullptr);

			auto  resourceManager = gfxBase->GetResourceManagerCpy();
			auto* device          = gfxBase->GetDevice();

			auto cmdListDesc  = bgl::CommandListDesc();
			cmdListDesc.type  = bgl::QueueType::kGraphics;
			auto cmdAllocator = device->CreateCommandAllocator();
			auto cmdList  = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
			auto cmdQueue = device->CreateCommandQueue(bgl::QueueType::kGraphics);

			auto outDesc         = bgl::ComputeBufferDesc();
			outDesc.initialCount = 1;
			outDesc.debugName    = "Probe Result";
			outDesc.SetElement<glm::vec4>();
			const bgl::BufferHandle outBuffer = resourceManager->CreateComputeBuffer(outDesc);
			REQUIRE(resourceManager->ValidBufferHandle(outBuffer));

			auto rbDesc                        = bgl::ReadbackBufferDesc();
			rbDesc.byteSize                    = sizeof(glm::vec4);
			rbDesc.debugName                   = "Probe Readback";
			const bgl::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

			auto lut = TonemapLut();
			lut.Init(resourceManager, c_TonemapLutFile);
			const SamplerHandle lutSampler = resourceManager->CreateSampler(
				SamplerDesc().SetAllFilters(true).SetAllAddressModes(SamplerAddressMode::kClamp));

			auto kernel = device->CreateComputeKernel(
				bgl::ComputePipelineDesc()
					.SetShader(device->CreateShader(shader))
					.SetDebugName(shader));
			REQUIRE(kernel.pipeline != nullptr);
			REQUIRE(kernel.uniforms.contains("gUniforms"));

			kernel["gUniforms"]["outColor"]   = outBuffer;
			kernel["gUniforms"]["lut"]        = lut.GetSrv();
			kernel["gUniforms"]["lutSampler"] = lutSampler;
			bind(kernel["gUniforms"]);

			cmdList->Open(cmdQueue, cmdAllocator);
			lut.Upload(cmdList.Get());

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
			resourceManager->DestroySampler(lutSampler, false);
			lut.Release();

			return result;
		}
	}

	glm::vec4
	RunAgX(bgl::IGraphics& gfx, float sceneLinear)
	{
		return RunProbe(gfx, "CSAgxCalibration", [&](Uniforms& uniforms) {
			uniforms["sceneLinear"] = sceneLinear;
		});
	}

	glm::vec4
	RunGradedAgX(
		bgl::IGraphics&                gfx,
		glm::vec3                      sceneLinear,
		glm::vec2                      uv,
		const bgl::ColorGradeSettings& settings)
	{
		return RunProbe(gfx, "CSColorGradeProbe", [&](Uniforms& uniforms) {
			uniforms["sceneLinear"]  = sceneLinear;
			uniforms["uv"]           = uv;
			uniforms["whiteBalance"] = WhiteBalanceLmsScale(settings.temperature, settings.tint);
			uniforms["slope"]        = settings.slope;
			uniforms["offset"]       = settings.offset;
			uniforms["power"]        = settings.power;
			uniforms["saturation"]   = settings.saturation;
			uniforms["contrast"]     = settings.contrast;
			uniforms["vignetteIntensity"]  = settings.vignetteIntensity;
			uniforms["vignetteSmoothness"] = settings.vignetteSmoothness;
		});
	}
}
