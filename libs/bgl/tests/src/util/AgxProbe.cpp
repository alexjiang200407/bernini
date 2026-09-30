#include "util/AgxProbe.h"
#include "gfx/GraphicsBase.h"
#include "postprocess/TonemapLut.h"
#include "postprocess/color_grade.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/uniforms/Uniforms.h>
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

			auto cmdListDesc  = bgpu::CommandListDesc();
			cmdListDesc.type  = bgpu::QueueType::kGraphics;
			auto cmdAllocator = device->CreateCommandAllocator();
			auto cmdList  = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
			auto cmdQueue = device->CreateCommandQueue(bgpu::QueueType::kGraphics);

			auto outDesc         = bgpu::ComputeBufferDesc();
			outDesc.initialCount = 1;
			outDesc.debugName    = "Probe Result";
			outDesc.SetElement<glm::vec4>();
			const bgpu::BufferHandle outBuffer = resourceManager->CreateComputeBuffer(outDesc);
			REQUIRE(resourceManager->ValidBufferHandle(outBuffer));

			auto rbDesc                         = bgpu::ReadbackBufferDesc();
			rbDesc.byteSize                     = sizeof(glm::vec4);
			rbDesc.debugName                    = "Probe Readback";
			const bgpu::ReadbackBufferHandle rb = resourceManager->CreateReadbackBuffer(rbDesc);

			auto lut = TonemapLut();
			lut.Init(resourceManager, c_TonemapLutFile);
			const bgpu::SamplerHandle lutSampler = resourceManager->CreateSampler(
				bgpu::SamplerDesc().SetAllFilters(true).SetAllAddressModes(
					bgpu::SamplerAddressMode::kClamp));

			auto kernel = device->CreateComputeKernel(
				bgpu::ComputePipelineDesc()
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

			auto state   = bgpu::ComputeState();
			state.kernel = &kernel;
			cmdList->SetComputeState(state);
			cmdList->Dispatch(1, 1, 1);

			cmdList->Barrier(
				outBuffer,
				bgpu::BufferBarrierDesc()
					.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
					.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
					.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
					.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));

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
		return RunProbe(gfx, "CSAgxCalibration", [&](bgpu::Uniforms& uniforms) {
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
		return RunProbe(gfx, "CSColorGradeProbe", [&](bgpu::Uniforms& uniforms) {
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
