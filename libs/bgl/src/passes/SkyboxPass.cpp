#include "passes/SkyboxPass.h"
#include "cmd/CommandList.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
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
		constexpr auto c_MeshDxil  = "./shaders/Skybox_MSMain.dxil"sv;
		constexpr auto c_PixelDxil = "./shaders/Skybox_PSMain.dxil"sv;
		constexpr auto c_Src       = "Skybox"sv;
	}

	void
	SkyboxPass::Init(IDevice* device)
	{
		gassert(device != nullptr, "Device must be initialized");

		auto pipelineDesc = MeshletPipelineDesc();

		pipelineDesc.meshShader =
			device->CreateShader(std::string(c_MeshDxil), std::string(c_Src), "MSMain");
		pipelineDesc.pixelShader =
			device->CreateShader(std::string(c_PixelDxil), std::string(c_Src), "PSMain");

		pipelineDesc.AddRtvFormat(Format::SBGRA8_UNORM);
		pipelineDesc.SetDsvFormat(Format::D24S8);

		auto raster = RasterState();
		raster.SetFillMode(RasterFillMode::kSolid)
			.SetCullMode(RasterCullMode::kNone)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(true);

		auto depth = DepthStencilState{};
		depth.SetDepthTestEnable(true)
			.SetDepthWriteEnable(false)
			.SetDepthFunc(ComparisonFunc::LessOrEqual)
			.SetStencilEnable(false);

		pipelineDesc.renderState = RenderState().SetRasterState(raster).SetDepthStencilState(depth);

		m_Kernel = device->CreateMeshletKernel(pipelineDesc);
	}

	void
	SkyboxPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		if (!draw.skybox.has_value())
		{
			return;
		}

		auto desc = PassDesc();

		desc.SetName(std::format("Skybox {}", draw.drawIdx))
			.AddTextureArg(
				TextureArg{ draw.backBufferName,
		                    BarrierSyncFlag::kRenderTarget,
		                    BarrierAccessFlag::kRenderTarget,
		                    BarrierLayout::kRenderTarget });

		desc.SetExec([this, draw](const PassContext& resources) { Execute(draw, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	SkyboxPass::Execute(const DrawData& draw, const PassContext& resources)
	{
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(m_Kernel.pipeline.IsInitialized(), "Skybox pipeline must be initialized");
		gassert(draw.skybox.has_value(), "SkyboxPass executed without a valid skybox");

		if (auto found = m_Kernel.FindUniforms("skyboxData"))
		{
			auto& skybox = *found;

			skybox["clipToWorld"] = draw.skyboxClipToWorld;

			if (auto u = skybox["cubeTex"]; u.IsValid())
			{
				u = draw.skybox->skyboxCubeTex;
			}
			if (auto u = skybox["sampler"]; u.IsValid())
			{
				u = draw.linearClampSampler;
			}
			if (auto u = skybox["exposure"]; u.IsValid())
			{
				u = draw.skybox->exposure;
			}
			if (auto u = skybox["mipLevel"]; u.IsValid())
			{
				u = static_cast<float>(draw.skybox->mipLevel);
			}
		}
		else
		{
			gfatal("Skybox shader is missing its 'skyboxData' constant buffer");
		}

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewport);
		gfxState.frameBuffer = FrameBuffer()
		                           .AddColorAttachment(draw.backBufferHandle)
		                           .SetDepthAttachment(draw.depthBufferHandle);

		cmd->SetMeshletState(gfxState);

		// One thread group -> one triangle covering the screen.
		cmd->DispatchMesh(1, 1, 1);
	}
}
