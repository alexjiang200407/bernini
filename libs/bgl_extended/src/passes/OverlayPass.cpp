#include "passes/OverlayPass.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "overlay/Overlay.h"
#include "passes/BinderNames.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/FrameBuffer.h"
#include "resource/Shader.h"
#include "types/MeshletState.h"
#include "types/RenderState.h"
#include <core/math.h>

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "programs.overlay.Overlay"sv;

		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in Overlay.slang.
		constexpr auto c_Cbuffer = "gOverlayDraw"sv;

		constexpr std::array<std::string_view, 8> c_Fields = {
			"vertices"sv,  "indices"sv,     "texture"sv,    "sampler"sv,
			"transform"sv, "translation"sv, "targetSize"sv, "triangleCount"sv,
		};

		// Triangles one mesh group emits, tracking c_TrianglesPerGroup in Overlay.slang.
		constexpr uint32_t c_TrianglesPerGroup = 64;
	}

	void
	OverlayPass::Init(IDevice* device)
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

		// Premultiplied, the form the shader returns; the backbuffer's alpha is opaque and stays so.
		auto blend = BlendState{};
		blend.SetRenderTarget(
			0,
			BlendState::RenderTarget{}
				.EnableBlend()
				.SetSrcBlend(BlendFactor::kOne)
				.SetDestBlend(BlendFactor::kInvSrcAlpha)
				.SetBlendOp(BlendOp::kAdd)
				.SetSrcBlendAlpha(BlendFactor::kOne)
				.SetDestBlendAlpha(BlendFactor::kInvSrcAlpha)
				.SetBlendOpAlpha(BlendOp::kAdd));

		pipelineDesc.renderState =
			RenderState().SetRasterState(raster).SetBlendState(blend).SetDepthStencilState(depth);

		m_Kernel = device->CreateMeshletKernel(pipelineDesc);

		BinderNames("OverlayPass"sv, { &m_Kernel, 1 }).Check(c_Cbuffer, c_Fields);
	}

	void
	OverlayPass::AttachToFrameGraph(FrameGraph& fg, const Args& args)
	{
		gassert(!args.draws.empty(), "An empty draw list attaches no overlay pass");

		auto desc = PassDesc();

		desc.SetName("Overlay").AddTextureArg(
			TextureArg{ std::string(c_BackbufferName),
		                BarrierSyncFlag::kRenderTarget,
		                BarrierAccessFlag::kRenderTarget,
		                BarrierLayout::kRenderTarget });

		desc.SetExec([this, args](const PassContext& resources) { Execute(args, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	OverlayPass::Execute(const Args& args, const PassContext& resources)
	{
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(m_Kernel.pipeline.IsInitialized(), "Overlay pipeline must be initialized");

		for (const core::SharedRef<Overlay>& overlay : args.overlays)
		{
			overlay->Flush(cmd);
		}

		auto found = m_Kernel.FindUniforms(c_Cbuffer);
		if (!found)
		{
			gfatal("Overlay shader is missing its '{}' constant buffer", c_Cbuffer);
		}

		auto& uniforms = *found;

		uniforms["sampler"]    = args.sampler;
		uniforms["targetSize"] = glm::vec2(
			args.viewport.maxX - args.viewport.minX,
			args.viewport.maxY - args.viewport.minY);

		for (const Draw& draw : args.draws)
		{
			if (draw.triangleCount == 0)
			{
				continue;
			}

			uniforms["vertices"]      = draw.vertices;
			uniforms["indices"]       = draw.indices;
			uniforms["texture"]       = draw.texture;
			uniforms["transform"]     = draw.transform;
			uniforms["translation"]   = draw.translation;
			uniforms["triangleCount"] = draw.triangleCount;

			auto gfxState   = MeshletState();
			gfxState.kernel = &m_Kernel;
			gfxState.viewportState.viewports.push_back(args.viewport);
			gfxState.viewportState.scissorRects.push_back(draw.scissor);
			gfxState.frameBuffer = FrameBuffer().AddColorAttachment(args.backBuffer);

			cmd->SetMeshletState(gfxState);

			cmd->DispatchMesh(core::div_ceil(draw.triangleCount, c_TrianglesPerGroup), 1, 1);
		}
	}
}
