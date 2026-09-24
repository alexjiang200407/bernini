#include "passes/BloomPass.h"
#include "cmd/CommandList.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "passes/BindingNameCheck.h"
#include "pipeline/MeshletPipeline.h"
#include "pipeline/PipelineBatch.h"
#include "resource/FrameBuffer.h"
#include "resource/Shader.h"
#include "types/Barrier.h"
#include "types/DepthStencilState.h"
#include "types/Format.h"
#include "types/RasterState.h"
#include "types/RenderState.h"
#include "types/ViewportState.h"
#include <array>
#include <bgl/Viewport.h>
#include <bgl_common/gassert.h>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "programs.screen.Bloom"sv;

		// The one fullscreen triangle every screen pass rasterizes; linked from its own module so
		// this file carries no copy of it.
		constexpr auto c_MeshSrc = "programs.screen.FullscreenRect"sv;

		// Keyed on the Slang globals' names as reflection reports them, so these must track the
		// ConstantBuffer declarations in Bloom.slang.
		constexpr auto c_DownsampleCbuffer = "gBloomDownsampleData"sv;
		constexpr auto c_UpsampleCbuffer   = "gBloomUpsampleData"sv;

		// Every member the Executes write. Kept beside the code that writes them so BindingNameCheck
		// catches a shader rename at startup: an optional write is silent, so a stale name would
		// otherwise resolve to nothing every frame and say nothing.
		constexpr std::array<std::string_view, 6> c_DownsampleFields = {
			"source"sv, "sampler"sv, "sourceTexelSize"sv, "threshold"sv, "knee"sv, "isFirstLevel"sv,
		};

		constexpr std::array<std::string_view, 5> c_UpsampleFields = {
			"coarse"sv, "fine"sv, "sampler"sv, "coarseTexelSize"sv, "scatter"sv,
		};

		MeshletPipelineDesc
		ScreenPipeline(IDevice* device, std::string_view pixelEntry)
		{
			auto pipelineDesc = MeshletPipelineDesc();

			pipelineDesc.meshShader = device->CreateShader(std::string(c_MeshSrc), "MSMain");
			pipelineDesc.pixelShader =
				device->CreateShader(std::string(c_Src), std::string(pixelEntry));

			pipelineDesc.AddRtvFormat(Format::RGBA16_FLOAT);

			auto raster = RasterState();
			raster.SetFillMode(RasterFillMode::kSolid)
				.SetCullMode(RasterCullMode::kNone)
				.SetFrontCounterClockwise(true)
				.SetDepthClipEnable(false);

			auto depth = DepthStencilState{};
			depth.SetDepthTestEnable(false).SetDepthWriteEnable(false).SetStencilEnable(false);

			pipelineDesc.renderState =
				RenderState().SetRasterState(raster).SetDepthStencilState(depth);

			return pipelineDesc;
		}
	}

	void
	BloomPass::Init(const PassInitContext& ctx)
	{
		gassert(ctx.device != nullptr, "Device must be initialized");

		ctx.pipelines->Add(m_DownsampleKernel, ScreenPipeline(ctx.device, "PSDownsample"));
		ctx.pipelines->Add(m_UpsampleKernel, ScreenPipeline(ctx.device, "PSUpsample"));
	}

	void
	BloomPass::CheckBindings() const
	{
		BindingNameCheck("BloomPass"sv, { &m_DownsampleKernel, 1 })
			.Check(c_DownsampleCbuffer, c_DownsampleFields);
		BindingNameCheck("BloomPass"sv, { &m_UpsampleKernel, 1 })
			.Check(c_UpsampleCbuffer, c_UpsampleFields);
	}

	void
	BloomPass::AttachToFrameGraph(FrameGraph& fg, const Args& args)
	{
		gassert(!args.levels.empty(), "A bloom chain must hold at least one level");

		// Held on the pass rather than captured: a copy per lambda would clone the level vector
		// and its strings into every pass of every frame. The execs run before the next attach,
		// so one frame's args are never overwritten while still wanted.
		m_Args = args;

		const uint32_t levelCount = static_cast<uint32_t>(args.levels.size());

		for (uint32_t i = 0; i < levelCount; ++i)
		{
			auto desc = PassDesc();

			desc.SetName(std::format("BloomDown{}", i))
				.AddTextureRead(
					i == 0 ? args.sourceName : args.levels[i - 1].downName,
					BarrierSyncFlag::kPixelShader)
				.AddRenderTarget(args.levels[i].downName);

			desc.SetExec([this, i](const PassContext& resources) {
				ExecuteDownsample(m_Args, i, resources);
			});

			fg.AddPass(std::move(desc));
		}

		for (uint32_t i = levelCount - 1; i-- > 0;)
		{
			auto desc = PassDesc();

			const bool coarsest = i + 2 == levelCount;

			desc.SetName(std::format("BloomUp{}", i))
				.AddTextureRead(
					coarsest ? args.levels[i + 1].downName : args.levels[i + 1].upName,
					BarrierSyncFlag::kPixelShader)
				.AddTextureRead(args.levels[i].downName, BarrierSyncFlag::kPixelShader)
				.AddRenderTarget(args.levels[i].upName);

			desc.SetExec(
				[this, i](const PassContext& resources) { ExecuteUpsample(m_Args, i, resources); });

			fg.AddPass(std::move(desc));
		}
	}

	void
	BloomPass::ExecuteDownsample(const Args& args, uint32_t level, const PassContext& resources)
	{
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(m_DownsampleKernel.pipeline.IsInitialized(), "Bloom pipeline must be initialized");

		const LevelArgs& target = args.levels[level];

		const glm::vec2 sourceSize = level == 0 ?
		                                 args.sourceSize :
		                                 glm::vec2(
											 static_cast<float>(args.levels[level - 1].width),
											 static_cast<float>(args.levels[level - 1].height));

		if (auto found = m_DownsampleKernel.FindUniforms(c_DownsampleCbuffer))
		{
			auto& uniforms = *found;

			uniforms["source"].SetIfValid(
				level == 0 ? args.source : args.levels[level - 1].downSrv);
			uniforms["sampler"].SetIfValid(args.sampler);
			uniforms["sourceTexelSize"].SetIfValid(glm::vec2(1.0f) / sourceSize);
			uniforms["threshold"].SetIfValid(args.threshold);
			uniforms["knee"].SetIfValid(args.knee);
			uniforms["isFirstLevel"].SetIfValid(level == 0 ? 1u : 0u);
		}
		else
		{
			gfatal("Bloom shader is missing its '{}' constant buffer", c_DownsampleCbuffer);
		}

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_DownsampleKernel;
		gfxState.viewportState.AddViewportAndScissorRect(
			Viewport(static_cast<float>(target.width), static_cast<float>(target.height)));
		gfxState.frameBuffer = FrameBuffer().AddColorAttachment(target.downRtv);

		cmd->SetMeshletState(gfxState);

		cmd->DispatchMesh(1, 1, 1);
	}

	void
	BloomPass::ExecuteUpsample(const Args& args, uint32_t level, const PassContext& resources)
	{
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(m_UpsampleKernel.pipeline.IsInitialized(), "Bloom pipeline must be initialized");

		const LevelArgs& target = args.levels[level];
		const LevelArgs& below  = args.levels[level + 1];

		const bool coarsest = level + 2 == static_cast<uint32_t>(args.levels.size());

		if (auto found = m_UpsampleKernel.FindUniforms(c_UpsampleCbuffer))
		{
			auto& uniforms = *found;

			uniforms["coarse"].SetIfValid(coarsest ? below.downSrv : below.upSrv);
			uniforms["fine"].SetIfValid(target.downSrv);
			uniforms["sampler"].SetIfValid(args.sampler);
			uniforms["coarseTexelSize"].SetIfValid(
				glm::vec2(1.0f) /
				glm::vec2(static_cast<float>(below.width), static_cast<float>(below.height)));
			uniforms["scatter"].SetIfValid(args.scatter);
		}
		else
		{
			gfatal("Bloom shader is missing its '{}' constant buffer", c_UpsampleCbuffer);
		}

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_UpsampleKernel;
		gfxState.viewportState.AddViewportAndScissorRect(
			Viewport(static_cast<float>(target.width), static_cast<float>(target.height)));
		gfxState.frameBuffer = FrameBuffer().AddColorAttachment(target.upRtv);

		cmd->SetMeshletState(gfxState);

		cmd->DispatchMesh(1, 1, 1);
	}
}
