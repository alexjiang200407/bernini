#include "passes/HzbBuildPass.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/frame_constants.h"
#include "passes/BindingNameCheck.h"
#include "passes/DrawData.h"
#include "scene/CullState.h"
#include "scene/HzbChain.h"
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
#include <bgpu/types/RasterState.h>
#include <bgpu/types/RenderState.h>
#include <bgpu/types/Viewport.h>
#include <bgpu/types/ViewportState.h>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr auto c_Src     = "programs.culling.HzbReduce"sv;
		constexpr auto c_MeshSrc = "programs.screen.FullscreenRect"sv;
		constexpr auto c_Cbuffer = "gHzbReduceData"sv;

		constexpr std::array<std::string_view, 3> c_Fields = {
			"source"sv,
			"sourceSize"sv,
			"destSize"sv,
		};

		constexpr auto c_HzbFormat = bgpu::Format::R32_FLOAT;
	}

	HzbBuildPass::HzbBuildPass(const PassInitContext& ctx)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		auto pipelineDesc        = bgpu::MeshletPipelineDesc();
		pipelineDesc.meshShader  = ctx.device->CreateShader(std::string(c_MeshSrc), "MSMain");
		pipelineDesc.pixelShader = ctx.device->CreateShader(std::string(c_Src), "PSMain");
		pipelineDesc.AddRtvFormat(c_HzbFormat);

		auto raster = bgpu::RasterState();
		raster.SetFillMode(bgpu::RasterFillMode::kSolid)
			.SetCullMode(bgpu::RasterCullMode::kNone)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(false);

		auto depth = bgpu::DepthStencilState{};
		depth.SetDepthTestEnable(false).SetDepthWriteEnable(false).SetStencilEnable(false);

		pipelineDesc.renderState =
			bgpu::RenderState().SetRasterState(raster).SetDepthStencilState(depth);

		ctx.pipelines->Add(m_Kernel, pipelineDesc);
	}

	void
	HzbBuildPass::CheckBindings() const
	{
		BindingNameCheck("HzbBuildPass"sv, { &m_Kernel, 1 }).Check(c_Cbuffer, c_Fields);
	}

	void
	HzbBuildPass::AttachToFrameGraph(
		FrameGraph&            fg,
		const DrawData&        draw,
		const std::string_view label)
	{
		core::ensure(draw.cullState != nullptr, "An HZB build needs the draw's cull state");
		const std::span<const HzbChain::Level> levels = draw.cullState->GetHzb().GetLevels();

		for (uint32_t i = 0; i < static_cast<uint32_t>(levels.size()); ++i)
		{
			auto desc = PassDesc();
			desc.SetName("HZB {} {}.{}", label, draw.drawIdx, i)
				.AddTextureRead(
					i == 0 ? std::string(c_DepthName) : HzbLevelName(i - 1),
					bgpu::BarrierSyncFlag::kPixelShader)
				.AddRenderTarget(HzbLevelName(i));
			desc.SetExec([this, draw, i](const PassContext& resources) {
				ExecuteLevel(draw, i, resources);
			});
			fg.AddPass(std::move(desc));
		}
	}

	void
	HzbBuildPass::ExecuteLevel(
		const DrawData&    draw,
		const uint32_t     level,
		const PassContext& resources)
	{
		bgpu::ICommandList* cmd = resources.GetCommandList();
		core::ensure(cmd != nullptr, "Pass commandlist must be initialized");
		core::ensure(m_Kernel.pipeline.IsInitialized(), "HZB pipeline must be initialized");

		const HzbChain&                        chain  = draw.cullState->GetHzb();
		const std::span<const HzbChain::Level> levels = chain.GetLevels();
		const HzbChain::Level&                 target = levels[level];

		const bgpu::SrvHandle source = level == 0 ? draw.targets.depthSrv : levels[level - 1].srv;
		const glm::uvec2      sourceSize =
			level == 0 ? glm::uvec2(chain.GetDepthWidth(), chain.GetDepthHeight()) :
						 glm::uvec2(levels[level - 1].width, levels[level - 1].height);

		if (auto found = m_Kernel.FindUniforms(c_Cbuffer))
		{
			auto& uniforms = *found;
			uniforms["source"].SetIfValid(source);
			uniforms["sourceSize"].SetIfValid(sourceSize);
			uniforms["destSize"].SetIfValid(glm::uvec2(target.width, target.height));
		}
		else
		{
			core::fatal("HZB shader is missing its '{}' constant buffer", c_Cbuffer);
		}

		auto state   = bgpu::MeshletState();
		state.kernel = &m_Kernel;
		state.viewportState.AddViewportAndScissorRect(
			bgpu::Viewport(static_cast<float>(target.width), static_cast<float>(target.height)));
		state.frameBuffer = bgpu::FrameBuffer().AddColorAttachment(target.rtv);
		cmd->SetMeshletState(state);
		cmd->DispatchMesh(1, 1, 1);
	}
}
