#include "passes/PostProcessPass.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/FrameBuffer.h"
#include "resource/Shader.h"
#include "types/RenderState.h"

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "PostProcess"sv;
	}

	void
	PostProcessPass::Init(IDevice* device)
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

		m_Kernel = device->CreateMeshletKernel(pipelineDesc);
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

		// Keyed on the Slang global's name as reflection reports it, so this string must track the
		// ConstantBuffer declaration in PostProcess.slang.
		if (auto found = m_Kernel.FindUniforms("gPostProcessData"))
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
			gfatal("PostProcess shader is missing its 'gPostProcessData' constant buffer");
		}

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(args.viewport);
		gfxState.frameBuffer = FrameBuffer().AddColorAttachment(args.backBuffer);

		cmd->SetMeshletState(gfxState);

		cmd->DispatchMesh(1, 1, 1);
	}
}
