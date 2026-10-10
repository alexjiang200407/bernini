#include "passes/WaterSceneCopyPass.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/frame_constants.h"
#include "passes/BindingNameCheck.h"
#include "passes/DrawData.h"
#include <array>
#include <bgl/ISceneView.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/pipeline/PipelineBatch.h>
#include <bgpu/resource/FrameBuffer.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/DepthStencilState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/MeshletState.h>
#include <bgpu/types/RasterState.h>
#include <bgpu/types/RenderState.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/err/util.h>
#include <string>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr auto c_MeshSrc  = "programs.screen.FullscreenRect"sv;
		constexpr auto c_PixelSrc = "programs.forward.WaterSceneCopy"sv;

		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in WaterSceneCopy.slang.
		constexpr auto c_Cbuffer = "gWaterSceneCopyData"sv;

		constexpr std::array<std::string_view, 1> c_Fields = { "sceneColor"sv };
	}

	WaterSceneCopyPass::WaterSceneCopyPass(const PassInitContext& ctx)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		auto pipelineDesc = bgpu::MeshletPipelineDesc();

		pipelineDesc.meshShader  = ctx.device->CreateShader(std::string(c_MeshSrc), "MSMain");
		pipelineDesc.pixelShader = ctx.device->CreateShader(std::string(c_PixelSrc), "PSMain");

		pipelineDesc.AddRtvFormat(bgpu::Format::RGBA16_FLOAT);

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
	WaterSceneCopyPass::CheckBindings() const
	{
		BindingNameCheck("WaterSceneCopyPass"sv, { &m_Kernel, 1 }).Check(c_Cbuffer, c_Fields);
	}

	void
	WaterSceneCopyPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		if (draw.targets.waterCopy.copy.IsNull())
		{
			return;
		}

		auto desc = PassDesc();

		desc.SetName("Water Scene Copy {}", draw.drawIdx)
			.AddTextureRead(c_SceneColorName, bgpu::BarrierSyncFlag::kPixelShader)
			.AddRenderTarget(c_SceneColorCopyName);

		desc.SetExec([this, draw](const PassContext& resources) { Execute(draw, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	WaterSceneCopyPass::Execute(const DrawData& draw, const PassContext& resources)
	{
		bgpu::ICommandList* cmd = resources.GetCommandList();

		core::ensure(cmd != nullptr, "Pass commandlist must be initialized");
		core::ensure(
			m_Kernel.pipeline.IsInitialized(),
			"Water Scene Copy pipeline must be initialized");

		if (auto found = m_Kernel.FindUniforms(c_Cbuffer))
		{
			(*found)["sceneColor"] = draw.targets.waterCopy.source;
		}
		else
		{
			core::fatal("Water Scene Copy shader is missing its '{}' constant buffer", c_Cbuffer);
		}

		auto gfxState   = bgpu::MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		gfxState.frameBuffer = bgpu::FrameBuffer().AddColorAttachment(draw.targets.waterCopy.copy);

		cmd->SetMeshletState(gfxState);

		// One thread group -> one triangle covering the viewport.
		cmd->DispatchMesh(1, 1, 1);
	}
}
