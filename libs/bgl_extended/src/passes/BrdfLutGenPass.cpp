#include "passes/BrdfLutGenPass.h"
#include "pipeline/PipelineBatch.h"
#include "resource/Shader.h"
#include <bgl_common/gassert.h>

#include "cmd/CommandList.h"
#include "device/Device.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/FrameBuffer.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv.h"
#include "resource/Texture.h"
#include "types/Barrier.h"
#include "types/DepthStencilState.h"
#include "types/Format.h"
#include "types/RasterState.h"
#include "types/RenderState.h"
#include "types/TextureDimension.h"
#include <bgl/IGraphics.h>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "programs.env.BrdfLut"sv;

		// Two channels: the integral factors into a scale and a bias on F0, and nothing else is
		// stored. Half float covers [0,1] with far more precision than the bilinear fetch resolves.
		constexpr Format c_Format = Format::RG16_FLOAT;
	}

	void
	BrdfLutGenPass::Init(const PassInitContext& ctx)
	{
		gassert(ctx.device != nullptr, "Device must be initialized");

		m_ResourceManager = ctx.resourceManager;

		auto pipelineDesc        = MeshletPipelineDesc();
		pipelineDesc.meshShader  = ctx.device->CreateShader(std::string(c_Src), "MSMain");
		pipelineDesc.pixelShader = ctx.device->CreateShader(std::string(c_Src), "PSMain");
		pipelineDesc.AddRtvFormat(c_Format);

		auto raster = RasterState();
		raster.SetFillMode(RasterFillMode::kSolid)
			.SetCullMode(RasterCullMode::kNone)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(false);

		auto depth = DepthStencilState{};
		depth.SetDepthTestEnable(false).SetDepthWriteEnable(false).SetStencilEnable(false);

		pipelineDesc.renderState = RenderState().SetRasterState(raster).SetDepthStencilState(depth);

		ctx.pipelines->Add(m_Kernel, std::move(pipelineDesc));
	}

	void
	BrdfLutGenPass::Generate(ICommandList* cmdList)
	{
		gassert(cmdList != nullptr, "Command list must be initialized");
		gassert(m_Kernel.pipeline.IsInitialized(), "BRDF LUT pipeline must be initialized");
		gassert(!Generated(), "BRDF LUT is generated at most once");

		auto textureDesc      = TextureDesc();
		textureDesc.format    = c_Format;
		textureDesc.width     = c_Dimension;
		textureDesc.height    = c_Dimension;
		textureDesc.dimension = TextureDimension::kTexture2D;
		textureDesc.debugName = "BRDF LUT";
		textureDesc.usage = TextureUsage{ TextureUsageFlag::kRenderTarget, TextureUsageFlag::kSRV };
		textureDesc.initialLayout = BarrierLayout::kRenderTarget;
		textureDesc.clearValue.SetColor(Color(0.0f, 0.0f, 0.0f, 0.0f));

		m_Texture = m_ResourceManager->CreateTexture(textureDesc);
		if (m_Texture.IsNull())
			throw GraphicsError("BRDF LUT texture could not be created");

		auto srvDesc      = SrvDesc();
		srvDesc.format    = c_Format;
		srvDesc.dimension = TextureDimension::kTexture2D;
		srvDesc.debugName = "BRDF LUT SRV";

		m_Srv = m_ResourceManager->CreateSrv(m_Texture, srvDesc);
		if (m_Srv.IsNull())
			throw GraphicsError("BRDF LUT SRV could not be created");

		auto rtvDesc      = RtvDesc();
		rtvDesc.format    = c_Format;
		rtvDesc.debugName = "BRDF LUT RTV";

		const RtvHandle rtv = m_ResourceManager->CreateRtv(m_Texture, rtvDesc);

		cmdList->BeginEvent("BRDF LUT");

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(
			Viewport(static_cast<float>(c_Dimension), static_cast<float>(c_Dimension)));
		gfxState.frameBuffer = FrameBuffer().AddColorAttachment(rtv);

		cmdList->SetMeshletState(gfxState);
		cmdList->DispatchMesh(1, 1, 1);

		TextureBarrierDesc barrier;
		barrier.syncBefore   = BarrierSyncFlag::kRenderTarget;
		barrier.accessBefore = BarrierAccessFlag::kRenderTarget;
		barrier.layoutBefore = BarrierLayout::kRenderTarget;
		barrier.syncAfter    = BarrierSyncFlag::kPixelShader;
		barrier.accessAfter  = BarrierAccessFlag::kShaderResource;
		barrier.layoutAfter  = BarrierLayout::kShaderResource;

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
