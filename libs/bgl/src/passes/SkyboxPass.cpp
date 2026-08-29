#include "passes/SkyboxPass.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "passes/BinderNames.h"
#include "passes/DrawData.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/FrameBuffer.h"
#include "resource/Shader.h"
#include "types/RenderState.h"
#include <bgl/ISceneView.h>

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "Skybox"sv;

		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in Skybox.slang.
		constexpr auto c_Cbuffer = "gSkyboxData"sv;

		// Every member Execute writes. Kept beside the code that writes them so
		// BinderNames catches a shader rename at startup: an optional write is silent, so
		// a stale name would otherwise resolve to nothing every frame and say nothing.
		constexpr std::array<std::string_view, 8> c_Fields = {
			"clipToWorld"sv, "prevWorldToClip"sv, "cubeTex"sv, "sampler"sv,
			"exposure"sv,    "mipLevel"sv,        "jitter"sv,  "prevJitter"sv,
		};
	}

	void
	SkyboxPass::Init(IDevice* device, PipelineBuild& build)
	{
		gassert(device != nullptr, "Device must be initialized");

		build.Step("Skybox"sv);

		auto pipelineDesc = MeshletPipelineDesc();

		pipelineDesc.meshShader  = device->CreateShader(std::string(c_Src), "MSMain");
		pipelineDesc.pixelShader = device->CreateShader(std::string(c_Src), "PSMain");

		pipelineDesc.AddRtvFormat(Format::RGBA16_FLOAT);
		pipelineDesc.AddRtvFormat(Format::RG16_FLOAT);
		pipelineDesc.SetDsvFormat(Format::D24S8);

		auto raster = RasterState();
		raster.SetFillMode(RasterFillMode::kSolid)
			.SetCullMode(RasterCullMode::kNone)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(true);

		auto depth = DepthStencilState{};
		depth.SetDepthTestEnable(true)
			.SetDepthWriteEnable(false)
			.SetDepthFunc(ComparisonFunc::kLessOrEqual)
			.SetStencilEnable(false);

		pipelineDesc.renderState = RenderState().SetRasterState(raster).SetDepthStencilState(depth);

		m_Kernel = device->CreateMeshletKernel(pipelineDesc);

		BinderNames("SkyboxPass"sv, { &m_Kernel, 1 }).Check(c_Cbuffer, c_Fields);
	}

	void
	SkyboxPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		if (!draw.lighting.skybox.has_value())
		{
			return;
		}

		auto desc = PassDesc();

		desc.SetName("Skybox {}", draw.drawIdx)
			.AddTextureArg(
				TextureArg{ std::string(c_BackbufferName),
		                    BarrierSyncFlag::kRenderTarget,
		                    BarrierAccessFlag::kRenderTarget,
		                    BarrierLayout::kRenderTarget })
			.AddTextureArg(
				TextureArg{ std::string(c_MotionVectorsName),
		                    BarrierSyncFlag::kRenderTarget,
		                    BarrierAccessFlag::kRenderTarget,
		                    BarrierLayout::kRenderTarget })
			.AddTextureArg(
				TextureArg{ std::string(c_DepthName),
		                    BarrierSyncFlag::kDepthStencil,
		                    BarrierAccessFlag::kDepthWrite,
		                    BarrierLayout::kDepthWrite });

		desc.SetExec([this, draw](const PassContext& resources) { Execute(draw, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	SkyboxPass::Execute(const DrawData& draw, const PassContext& resources)
	{
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(m_Kernel.pipeline.IsInitialized(), "Skybox pipeline must be initialized");
		gassert(draw.lighting.skybox.has_value(), "SkyboxPass executed without a valid skybox");

		if (auto found = m_Kernel.FindUniforms(c_Cbuffer))
		{
			auto& skybox = *found;

			skybox["clipToWorld"]     = draw.lighting.skyboxClipToWorld;
			skybox["prevWorldToClip"] = draw.lighting.skyboxPrevWorldToClip;

			skybox["cubeTex"].SetIfValid(draw.lighting.skybox->skyboxCubeTex);
			skybox["sampler"].SetIfValid(draw.samplers.linearClamp);
			skybox["exposure"].SetIfValid(draw.lighting.SkyExposure());
			skybox["mipLevel"].SetIfValid(static_cast<float>(draw.lighting.skybox->mipLevel));
			skybox["jitter"].SetIfValid(draw.viewState.jitter);
			skybox["prevJitter"].SetIfValid(draw.viewState.prevJitter);
		}
		else
		{
			gfatal("Skybox shader is missing its '{}' constant buffer", c_Cbuffer);
		}

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		gfxState.frameBuffer = FrameBuffer()
		                           .AddColorAttachment(draw.targets.sceneColor)
		                           .AddColorAttachment(draw.targets.motionVector)
		                           .SetDepthAttachment(draw.targets.depth);

		cmd->SetMeshletState(gfxState);

		// One thread group -> one triangle covering the screen.
		cmd->DispatchMesh(1, 1, 1);
	}
}
