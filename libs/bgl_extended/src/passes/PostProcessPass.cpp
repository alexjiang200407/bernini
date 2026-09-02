#include "passes/PostProcessPass.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "passes/BinderNames.h"
#include "pipeline/MeshletPipeline.h"
#include "pipeline/PipelineBatch.h"
#include "resource/FrameBuffer.h"
#include "resource/Shader.h"
#include "types/RenderState.h"

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "programs.screen.PostProcess"sv;

		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in PostProcess.slang.
		constexpr auto c_Cbuffer = "gPostProcessData"sv;

		// Every member Execute writes. Kept beside the code that writes them so
		// BinderNames catches a shader rename at startup: an optional write is silent, so
		// a stale name would otherwise resolve to nothing every frame and say nothing.
		constexpr std::array<std::string_view, 6> c_Fields = {
			"sceneColor"sv,     "sampler"sv,     "maskSampler"sv,
			"outlineEnabled"sv, "outlineMask"sv, "maskSize"sv,
		};
	}

	void
	PostProcessPass::Init(IDevice* device, PipelineBatch& pipelines)
	{
		gassert(device != nullptr, "Device must be initialized");

		auto pipelineDesc = MeshletPipelineDesc();

		pipelineDesc.meshShader  = device->CreateShader(std::string(c_Src), "MSMain");
		pipelineDesc.pixelShader = device->CreateShader(std::string(c_Src), "PSMain");

		pipelineDesc.AddRtvFormat(Format::SBGRA8_UNORM);

		auto raster = RasterState();
		raster.SetFillMode(RasterFillMode::kSolid)
			.SetCullMode(RasterCullMode::kNone)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(false);

		auto depth = DepthStencilState{};
		depth.SetDepthTestEnable(false).SetDepthWriteEnable(false).SetStencilEnable(false);

		pipelineDesc.renderState = RenderState().SetRasterState(raster).SetDepthStencilState(depth);

		pipelines.Add(m_Kernel, std::move(pipelineDesc));
	}

	void
	PostProcessPass::CheckBindings() const
	{
		BinderNames("PostProcessPass"sv, { &m_Kernel, 1 }).Check(c_Cbuffer, c_Fields);
	}

	void
	PostProcessPass::AttachToFrameGraph(FrameGraph& fg, const Args& args)
	{
		auto desc = PassDesc();

		desc.SetName("PostProcess")
			.AddTextureArg(
				TextureArg{ args.sourceName,
		                    BarrierSyncFlag::kPixelShader,
		                    BarrierAccessFlag::kShaderResource,
		                    BarrierLayout::kShaderResource })
			.AddTextureArg(
				TextureArg{ std::string(c_BackbufferName),
		                    BarrierSyncFlag::kRenderTarget,
		                    BarrierAccessFlag::kRenderTarget,
		                    BarrierLayout::kRenderTarget });

		if (args.outlineEnabled)
		{
			desc.AddTextureArg(
				TextureArg{ std::string(c_OutlineMaskName),
			                BarrierSyncFlag::kPixelShader,
			                BarrierAccessFlag::kShaderResource,
			                BarrierLayout::kShaderResource });
		}

		desc.SetExec([this, args](const PassContext& resources) { Execute(args, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	PostProcessPass::Execute(const Args& args, const PassContext& resources)
	{
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(m_Kernel.pipeline.IsInitialized(), "PostProcess pipeline must be initialized");

		if (auto found = m_Kernel.FindUniforms(c_Cbuffer))
		{
			auto& tonemap = *found;

			tonemap["sceneColor"].SetIfValid(args.source);
			tonemap["sampler"].SetIfValid(args.sampler);
			tonemap["maskSampler"].SetIfValid(args.maskSampler);
			tonemap["outlineEnabled"].SetIfValid(args.outlineEnabled ? 1u : 0u);
			if (args.outlineEnabled)
			{
				tonemap["outlineMask"].SetIfValid(args.outlineMask);
				tonemap["maskSize"].SetIfValid(args.maskSize);
			}
		}
		else
		{
			gfatal("PostProcess shader is missing its '{}' constant buffer", c_Cbuffer);
		}

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(args.viewport);
		gfxState.frameBuffer = FrameBuffer().AddColorAttachment(args.backBuffer);

		cmd->SetMeshletState(gfxState);

		cmd->DispatchMesh(1, 1, 1);
	}
}
