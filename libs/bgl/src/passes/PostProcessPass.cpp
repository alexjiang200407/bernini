#include "passes/PostProcessPass.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/frame_constants.h"
#include "passes/BindingNameCheck.h"
#include "postprocess/color_grade.h"
#include <algorithm>
#include <array>
#include <bgl/IRenderTarget.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/constants/constants.h>
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/pipeline/PipelineBatch.h>
#include <bgpu/resource/FrameBuffer.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/DepthStencilState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/RasterState.h>
#include <bgpu/types/RenderState.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "programs.screen.PostProcess"sv;

		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in PostProcess.slang.
		constexpr auto c_Cbuffer = "gPostProcessData"sv;

		// Every member Execute writes. Kept beside the code that writes them so
		// BindingNameCheck catches a shader rename at startup: an optional write is silent, so
		// a stale name would otherwise resolve to nothing every frame and say nothing.
		constexpr std::array<std::string_view, 31> c_Fields = {
			"sceneColor"sv,
			"sourceTexelSize"sv,
			"rcasStrength"sv,
			"sampler"sv,
			"maskSampler"sv,
			"outlineEnabled"sv,
			"outlineMask"sv,
			"maskSize"sv,
			"tonemapLut"sv,
			"lutSampler"sv,
			"bloom"sv,
			"bloomSampler"sv,
			"bloomIntensity"sv,
			"bloomEnabled"sv,
			"gradeWhiteBalance"sv,
			"gradeSlope"sv,
			"gradeOffset"sv,
			"gradePower"sv,
			"gradeSaturation"sv,
			"gradeContrast"sv,
			"gradeVignetteIntensity"sv,
			"gradeVignetteSmoothness"sv,
			"gradeEnabled"sv,
			"toon"sv,
			"splitOffset"sv,
			"splitRadial"sv,
			"splitEnabled"sv,
			"grainPitch"sv,
			"grainIntensity"sv,
			"grainPattern"sv,
			"grainEnabled"sv,
		};

		// The output height a look's pixel distances are authored at; they scale with the target's,
		// so a look keeps its share of the frame.
		constexpr float c_LookReferenceLines = 2160.0f;

		/**
		 * FSR 2's mapping from a sharpness in [0, 1] to RCAS's lobe scale: 2 - 2s stops below the
		 * maximum (ffx_fsr2.cpp, FsrRcasCon). Zero is off here rather than two stops, so a target that
		 * never asked for a sharpen draws the frame it always did.
		 */
		[[nodiscard]] float
		RcasStrength(float sharpness) noexcept
		{
			return sharpness > 0.0f ? std::exp2(2.0f * sharpness - 2.0f) : 0.0f;
		}
	}

	PostProcessPass::PostProcessPass(const PassInitContext& ctx)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		auto pipelineDesc = bgpu::MeshletPipelineDesc();

		pipelineDesc.meshShader  = ctx.device->CreateShader(std::string(c_Src), "MSMain");
		pipelineDesc.pixelShader = ctx.device->CreateShader(std::string(c_Src), "PSMain");

		pipelineDesc.AddRtvFormat(bgpu::Format::SBGRA8_UNORM);

		auto raster = bgpu::RasterState();
		raster.SetFillMode(bgpu::RasterFillMode::kSolid)
			.SetCullMode(bgpu::RasterCullMode::kNone)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(false);

		auto depth = bgpu::DepthStencilState{};
		depth.SetDepthTestEnable(false).SetDepthWriteEnable(false).SetStencilEnable(false);

		pipelineDesc.renderState =
			bgpu::RenderState().SetRasterState(raster).SetDepthStencilState(depth);

		ctx.pipelines->Add(m_Kernel, std::move(pipelineDesc));
	}

	void
	PostProcessPass::CheckBindings() const
	{
		BindingNameCheck("PostProcessPass"sv, { &m_Kernel, 1 }).Check(c_Cbuffer, c_Fields);
	}

	void
	PostProcessPass::AttachToFrameGraph(FrameGraph& fg, const Args& args)
	{
		auto desc = PassDesc();

		desc.SetName("PostProcess")
			.AddTextureRead(args.sourceName, bgpu::BarrierSyncFlag::kPixelShader)
			.AddRenderTarget(c_BackbufferName);

		if (args.outlineEnabled)
		{
			desc.AddTextureRead(c_OutlineMaskName, bgpu::BarrierSyncFlag::kPixelShader);
		}

		if (args.bloomEnabled)
		{
			desc.AddTextureRead(args.bloomName, bgpu::BarrierSyncFlag::kPixelShader);
		}

		desc.SetExec([this, args](const PassContext& resources) { Execute(args, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	PostProcessPass::Execute(const Args& args, const PassContext& resources)
	{
		bgpu::ICommandList* cmd = resources.GetCommandList();

		core::ensure(cmd != nullptr, "Pass commandlist must be initialized");
		core::ensure(m_Kernel.pipeline.IsInitialized(), "PostProcess pipeline must be initialized");

		if (auto found = m_Kernel.FindUniforms(c_Cbuffer))
		{
			auto& tonemap = *found;

			const auto outputSize = glm::vec2(
				args.viewport.maxX - args.viewport.minX,
				args.viewport.maxY - args.viewport.minY);

			tonemap["sceneColor"].SetIfValid(args.source);
			tonemap["rcasStrength"].SetIfValid(RcasStrength(args.taaSharpness));
			tonemap["sourceTexelSize"].SetIfValid(1.0f / outputSize);
			tonemap["sampler"].SetIfValid(args.sampler);
			tonemap["maskSampler"].SetIfValid(args.maskSampler);
			tonemap["tonemapLut"].SetIfValid(args.tonemapLut);
			tonemap["lutSampler"].SetIfValid(args.lutSampler);
			tonemap["outlineEnabled"].SetIfValid(args.outlineEnabled ? 1u : 0u);
			if (args.outlineEnabled)
			{
				tonemap["outlineMask"].SetIfValid(args.outlineMask);
				tonemap["maskSize"].SetIfValid(args.maskSize);
			}

			tonemap["bloomEnabled"].SetIfValid(args.bloomEnabled ? 1u : 0u);
			if (args.bloomEnabled)
			{
				tonemap["bloom"].SetIfValid(args.bloom);
				tonemap["bloomSampler"].SetIfValid(args.bloomSampler);
				tonemap["bloomIntensity"].SetIfValid(args.bloomIntensity);
			}

			tonemap["toon"].SetIfValid(args.postProcessType == PostProcessType::kToon ? 1u : 0u);
			tonemap["gradeEnabled"].SetIfValid(args.colorGradeEnabled ? 1u : 0u);
			if (args.colorGradeEnabled)
			{
				const ColorGradeSettings& grade = args.colorGrade;

				tonemap["gradeWhiteBalance"].SetIfValid(
					WhiteBalanceLmsScale(grade.temperature, grade.tint));
				tonemap["gradeSlope"].SetIfValid(grade.slope);
				tonemap["gradeOffset"].SetIfValid(grade.offset);
				tonemap["gradePower"].SetIfValid(grade.power);
				tonemap["gradeSaturation"].SetIfValid(grade.saturation);
				tonemap["gradeContrast"].SetIfValid(grade.contrast);
				tonemap["gradeVignetteIntensity"].SetIfValid(grade.vignetteIntensity);
				tonemap["gradeVignetteSmoothness"].SetIfValid(grade.vignetteSmoothness);
			}

			tonemap["splitEnabled"].SetIfValid(args.colorSplitEnabled ? 1u : 0u);
			if (args.colorSplitEnabled)
			{
				// A distance from the centre of one is half the height, so the radial share in uv
				// is the same number on both axes and at every output size.
				tonemap["splitOffset"].SetIfValid(
					args.colorSplit.offset * (outputSize.y / c_LookReferenceLines) / outputSize);
				tonemap["splitRadial"].SetIfValid(
					args.colorSplit.radial / (0.5f * c_LookReferenceLines));
			}

			tonemap["grainEnabled"].SetIfValid(args.filmGrainEnabled ? 1u : 0u);
			if (args.filmGrainEnabled)
			{
				const FilmGrainSettings& grain = args.filmGrain;

				tonemap["grainPitch"].SetIfValid(
					std::max(1.0f, grain.size * outputSize.y / c_LookReferenceLines));
				tonemap["grainIntensity"].SetIfValid(grain.intensity);
				tonemap["grainPattern"].SetIfValid(
					grain.holdFrames == 0 ?
						0u :
						static_cast<uint32_t>(args.frameCount / grain.holdFrames));
			}
		}
		else
		{
			core::fatal("PostProcess shader is missing its '{}' constant buffer", c_Cbuffer);
		}

		auto gfxState   = bgpu::MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(args.viewport);
		gfxState.frameBuffer = bgpu::FrameBuffer().AddColorAttachment(args.backBuffer);

		cmd->SetMeshletState(gfxState);

		cmd->DispatchMesh(1, 1, 1);
	}
}
