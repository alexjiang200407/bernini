#include "passes/TaaResolvePass.h"
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
		constexpr auto c_Src = "TaaResolve"sv;

		// How much of the resolved pixel is this frame. 0.1 converges in roughly the length of the
		// jitter sequence while leaving a short enough tail that a clamped-but-stale history does
		// not read as a trail.
		constexpr float c_BlendWeight = 0.1f;
	}

	void
	TaaResolvePass::Init(IDevice* device)
	{
		gassert(device != nullptr, "Device must be initialized");

		auto pipelineDesc = MeshletPipelineDesc();

		pipelineDesc.meshShader  = device->CreateShader(std::string(c_Src), "MSMain");
		pipelineDesc.pixelShader = device->CreateShader(std::string(c_Src), "PSMain");

		pipelineDesc.AddRtvFormat(Format::RGBA16_FLOAT);

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
	TaaResolvePass::AttachToFrameGraph(FrameGraph& fg, const Args& args)
	{
		auto desc = PassDesc();

		desc.SetName("TaaResolve")
			.AddTextureArg(
				TextureArg{ std::string(c_SceneColorName),
		                    BarrierSyncFlag::kPixelShader,
		                    BarrierAccessFlag::kShaderResource,
		                    BarrierLayout::kShaderResource })
			.AddTextureArg(
				TextureArg{ std::string(c_MotionVectorsName),
		                    BarrierSyncFlag::kPixelShader,
		                    BarrierAccessFlag::kShaderResource,
		                    BarrierLayout::kShaderResource })
			.AddTextureArg(
				TextureArg{ args.prevHistoryName,
		                    BarrierSyncFlag::kPixelShader,
		                    BarrierAccessFlag::kShaderResource,
		                    BarrierLayout::kShaderResource })
			.AddTextureArg(
				TextureArg{ args.historyName,
		                    BarrierSyncFlag::kRenderTarget,
		                    BarrierAccessFlag::kRenderTarget,
		                    BarrierLayout::kRenderTarget });

		desc.SetExec([this, args](const PassContext& resources) { Execute(args, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	TaaResolvePass::Execute(const Args& args, const PassContext& resources)
	{
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(m_Kernel.pipeline.IsInitialized(), "TaaResolve pipeline must be initialized");

		const float width  = args.viewport.maxX - args.viewport.minX;
		const float height = args.viewport.maxY - args.viewport.minY;

		// Keyed on the Slang global's name as reflection reports it, so this string must track the
		// ConstantBuffer declaration in TaaResolve.slang.
		if (auto found = m_Kernel.FindUniforms("gTaaResolveData"))
		{
			auto& taa = *found;

			if (auto u = taa["sceneColor"]; u.IsValid())
			{
				u = args.sceneColor;
			}
			if (auto u = taa["history"]; u.IsValid())
			{
				u = args.prevHistory;
			}
			if (auto u = taa["motionVectors"]; u.IsValid())
			{
				u = args.motionVectors;
			}
			if (auto u = taa["pointSampler"]; u.IsValid())
			{
				u = args.pointSampler;
			}
			if (auto u = taa["linearSampler"]; u.IsValid())
			{
				u = args.linearSampler;
			}
			if (auto u = taa["texelSize"]; u.IsValid())
			{
				u = glm::vec2(1.0f / width, 1.0f / height);
			}
			if (auto u = taa["blendWeight"]; u.IsValid())
			{
				u = c_BlendWeight;
			}
			if (auto u = taa["historyValid"]; u.IsValid())
			{
				u = args.historyValid ? 1.0f : 0.0f;
			}
		}
		else
		{
			gfatal("TaaResolve shader is missing its 'gTaaResolveData' constant buffer");
		}

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(args.viewport);
		gfxState.frameBuffer = FrameBuffer().AddColorAttachment(args.history);

		cmd->SetMeshletState(gfxState);

		cmd->DispatchMesh(1, 1, 1);
	}
}
