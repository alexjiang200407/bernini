#include "passes/BrdfLutGenPass.h"
#include <bgpu/pipeline/PipelineBatch.h>
#include <bgpu/resource/Shader.h>
#include <core/err/util.h>

#include <bgl/IGraphics.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/resource/FrameBuffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/DepthStencilState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/RasterState.h>
#include <bgpu/types/RenderState.h>
#include <bgpu/types/TextureDimension.h>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "programs.env.BrdfLut"sv;

		// Two channels: the integral factors into a scale and a bias on F0, and nothing else is
		// stored. Half float covers [0,1] with far more precision than the bilinear fetch resolves.
		constexpr bgpu::Format c_Format = bgpu::Format::RG16_FLOAT;
	}

	void
	BrdfLutGenPass::Init(const PassInitContext& ctx)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		m_ResourceManager = ctx.resourceManager;

		auto pipelineDesc        = bgpu::MeshletPipelineDesc();
		pipelineDesc.meshShader  = ctx.device->CreateShader(std::string(c_Src), "MSMain");
		pipelineDesc.pixelShader = ctx.device->CreateShader(std::string(c_Src), "PSMain");
		pipelineDesc.AddRtvFormat(c_Format);

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
	BrdfLutGenPass::Generate(bgpu::ICommandList* cmdList)
	{
		core::ensure(cmdList != nullptr, "Command list must be initialized");
		core::ensure(m_Kernel.pipeline.IsInitialized(), "BRDF LUT pipeline must be initialized");
		core::ensure(!Generated(), "BRDF LUT is generated at most once");

		auto textureDesc          = bgpu::TextureDesc();
		textureDesc.format        = c_Format;
		textureDesc.width         = c_Dimension;
		textureDesc.height        = c_Dimension;
		textureDesc.dimension     = bgpu::TextureDimension::kTexture2D;
		textureDesc.debugName     = "BRDF LUT";
		textureDesc.usage         = bgpu::TextureUsage{ bgpu::TextureUsageFlag::kRenderTarget,
			                                            bgpu::TextureUsageFlag::kSRV };
		textureDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
		textureDesc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 0.0f));

		m_Texture = m_ResourceManager->CreateTexture(textureDesc);
		if (m_Texture.IsNull())
			throw GraphicsError("BRDF LUT texture could not be created");

		auto srvDesc      = bgpu::SrvDesc();
		srvDesc.format    = c_Format;
		srvDesc.dimension = bgpu::TextureDimension::kTexture2D;
		srvDesc.debugName = "BRDF LUT SRV";

		m_Srv = m_ResourceManager->CreateSrv(m_Texture, srvDesc);
		if (m_Srv.IsNull())
			throw GraphicsError("BRDF LUT SRV could not be created");

		auto rtvDesc      = bgpu::RtvDesc();
		rtvDesc.format    = c_Format;
		rtvDesc.debugName = "BRDF LUT RTV";

		const bgpu::RtvHandle rtv = m_ResourceManager->CreateRtv(m_Texture, rtvDesc);

		cmdList->BeginEvent("BRDF LUT");

		auto gfxState   = bgpu::MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(
			bgpu::Viewport(static_cast<float>(c_Dimension), static_cast<float>(c_Dimension)));
		gfxState.frameBuffer = bgpu::FrameBuffer().AddColorAttachment(rtv);

		cmdList->SetMeshletState(gfxState);
		cmdList->DispatchMesh(1, 1, 1);

		bgpu::TextureBarrierDesc barrier;
		barrier.syncBefore   = bgpu::BarrierSyncFlag::kRenderTarget;
		barrier.accessBefore = bgpu::BarrierAccessFlag::kRenderTarget;
		barrier.layoutBefore = bgpu::BarrierLayout::kRenderTarget;
		barrier.syncAfter    = bgpu::BarrierSyncFlag::kPixelShader;
		barrier.accessAfter  = bgpu::BarrierAccessFlag::kShaderResource;
		barrier.layoutAfter  = bgpu::BarrierLayout::kShaderResource;

		cmdList->Barrier(m_Texture, barrier);
		cmdList->EndEvent();

		// Deferred: the recording above still references the view until the submission retires.
		m_ResourceManager->DestroyRtv(rtv, true);
	}

	void
	BrdfLutGenPass::Release() noexcept
	{
		if (m_ResourceManager == nullptr)
			return;

		if (!m_Srv.IsNull())
			m_ResourceManager->DestroySrv(m_Srv, false);
		if (!m_Texture.IsNull())
			m_ResourceManager->DestroyTexture(m_Texture, false);

		m_Kernel.Reset();
		m_ResourceManager = nullptr;
	}
}
