#include "passes/BackdropPass.h"
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
#include <bgpu/types/DepthStencilState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/RasterState.h>
#include <bgpu/types/RenderState.h>
#include <core/err/util.h>
#include <string>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "programs.env.Backdrop"sv;

		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in Backdrop.slang.
		constexpr auto c_Cbuffer = "gBackdropData"sv;

		constexpr std::array<std::string_view, 2> c_Fields = { "bottom"sv, "top"sv };
	}

	BackdropPass::BackdropPass(const PassInitContext& ctx)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		auto pipelineDesc = bgpu::MeshletPipelineDesc();

		pipelineDesc.meshShader  = ctx.device->CreateShader(std::string(c_Src), "MSMain");
		pipelineDesc.pixelShader = ctx.device->CreateShader(std::string(c_Src), "PSMain");

		pipelineDesc.AddRtvFormat(bgpu::Format::RGBA16_FLOAT);
		pipelineDesc.AddRtvFormat(c_MotionVectorFormat);
		pipelineDesc.SetDsvFormat(bgpu::Format::D24S8);

		auto raster = bgpu::RasterState();
		raster.SetFillMode(bgpu::RasterFillMode::kSolid)
			.SetCullMode(bgpu::RasterCullMode::kNone)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(true);

		auto depth = bgpu::DepthStencilState{};
		depth.SetDepthTestEnable(true)
			.SetDepthWriteEnable(false)
			.SetDepthFunc(bgpu::ComparisonFunc::kLessOrEqual)
			.SetStencilEnable(false);

		pipelineDesc.renderState =
			bgpu::RenderState().SetRasterState(raster).SetDepthStencilState(depth);

		ctx.pipelines->Add(m_Kernel, std::move(pipelineDesc));
	}

	void
	BackdropPass::CheckBindings() const
	{
		BindingNameCheck("BackdropPass"sv, { &m_Kernel, 1 }).Check(c_Cbuffer, c_Fields);
	}

	void
	BackdropPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		if (!draw.lighting.backdrop.has_value())
		{
			return;
		}

		auto desc = PassDesc();

		desc.SetName("Backdrop {}", draw.drawIdx)
			.AddRenderTarget(c_BackbufferName)
			.AddRenderTarget(c_MotionVectorsName)
			.AddDepthWrite(c_DepthName);

		desc.SetExec([this, draw](const PassContext& resources) { Execute(draw, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	BackdropPass::Execute(const DrawData& draw, const PassContext& resources)
	{
		bgpu::ICommandList* cmd = resources.GetCommandList();

		core::ensure(cmd != nullptr, "Pass commandlist must be initialized");
		core::ensure(m_Kernel.pipeline.IsInitialized(), "Backdrop pipeline must be initialized");
		core::ensure(
			draw.lighting.backdrop.has_value(),
			"BackdropPass executed without a backdrop");

		if (auto found = m_Kernel.FindUniforms(c_Cbuffer))
		{
			auto& backdrop     = *found;
			backdrop["bottom"] = draw.lighting.backdrop->bottom;
			backdrop["top"]    = draw.lighting.backdrop->top;
		}
		else
		{
			core::fatal("Backdrop shader is missing its '{}' constant buffer", c_Cbuffer);
		}

		auto gfxState   = bgpu::MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		gfxState.frameBuffer = bgpu::FrameBuffer()
		                           .AddColorAttachment(draw.targets.sceneColor)
		                           .AddColorAttachment(draw.targets.motionVector)
		                           .SetDepthAttachment(draw.targets.depth);

		cmd->SetMeshletState(gfxState);

		// One thread group -> one triangle covering the screen.
		cmd->DispatchMesh(1, 1, 1);
	}
}
