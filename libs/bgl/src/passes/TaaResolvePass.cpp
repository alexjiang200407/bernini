#include "passes/TaaResolvePass.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "passes/BinderNames.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/FrameBuffer.h"
#include "resource/Shader.h"
#include "types/RenderState.h"

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "TaaResolve"sv;

		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in TaaResolve.slang.
		constexpr auto c_Cbuffer = "gTaaResolveData"sv;

		// Every member Execute writes. Kept beside the code that writes them so
		// BinderNames catches a shader rename at startup: an optional write is silent, so
		// a stale name would otherwise resolve to nothing every frame and say nothing.
		constexpr std::array<std::string_view, 19> c_Fields = {
			"sceneColor"sv,      "history"sv,        "motionVectors"sv, "depth"sv,
			"clipToView"sv,      "viewToPrevClip"sv, "jitter"sv,        "cameraPairValid"sv,
			"pointSampler"sv,    "linearSampler"sv,  "renderSize"sv,    "renderTexelSize"sv,
			"outputTexelSize"sv, "jitterTexels"sv,   "subPixels"sv,     "resampling"sv,
			"sampleWeightK"sv,   "blendWeight"sv,    "historyValid"sv,
		};

		// How much of the resolved pixel is this frame. The trade is flicker against how fast the
		// antialiasing converges, *not* against ghosting -- the neighbourhood clamp is what bounds a
		// trail, and the weight barely moves it. Measured: 0.1 leaves 0.0022 of frame-to-frame noise
		// on a hashed surface, 0.05 leaves 0.0015, 0.025 leaves 0.0014 but no longer resolves an edge
		// within the frames a camera actually holds still for. This is the base: the resolve divides
		// it by remembered stochastic spread at rest (TaaResolve.slang), which is what reaches the
		// residual a constant weight cannot.
		constexpr float c_BlendWeight = 0.05f;
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

		BinderNames("TaaResolvePass"sv, { &m_Kernel, 1 }).Check(c_Cbuffer, c_Fields);
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
				TextureArg{ std::string(c_DepthName),
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

		const auto outputSize = glm::vec2(
			args.viewport.maxX - args.viewport.minX,
			args.viewport.maxY - args.viewport.minY);

		gassert(
			args.renderSize.x > 0.0f && args.renderSize.y > 0.0f,
			"TaaResolve needs a non-degenerate render size");

		// The jitter reaches the shader twice over: in NDC, which is where CameraMotion undoes it,
		// and in render texels, which is where the sample it moved actually landed.
		const glm::vec2 jitterTexels = args.jitter * glm::vec2(0.5f, -0.5f) * args.renderSize;

		gassert(
			args.reconstructionWidth > 0.0f,
			"TaaResolve needs a positive reconstruction width");

		// One phase unless the output grid is the denser of the two, which is what leaves the
		// reconstruction kernel at unity everywhere a render scale does not upscale.
		const auto subPixels = glm::vec2(
			std::max(1.0f, std::ceil(outputSize.x / args.renderSize.x)),
			std::max(1.0f, std::ceil(outputSize.y / args.renderSize.y)));

		if (auto found = m_Kernel.FindUniforms(c_Cbuffer))
		{
			auto& taa = *found;

			taa["sceneColor"].SetIfValid(args.sceneColor);
			taa["history"].SetIfValid(args.prevHistory);
			taa["motionVectors"].SetIfValid(args.motionVectors);
			taa["depth"].SetIfValid(args.depth);
			taa["clipToView"].SetIfValid(args.clipToView);
			taa["viewToPrevClip"].SetIfValid(args.viewToPrevClip);
			taa["jitter"].SetIfValid(args.jitter);
			taa["cameraPairValid"].SetIfValid(args.cameraPairValid ? 1.0f : 0.0f);
			taa["pointSampler"].SetIfValid(args.pointSampler);
			taa["linearSampler"].SetIfValid(args.linearSampler);
			taa["renderSize"].SetIfValid(args.renderSize);
			taa["renderTexelSize"].SetIfValid(1.0f / args.renderSize);
			taa["outputTexelSize"].SetIfValid(1.0f / outputSize);
			taa["jitterTexels"].SetIfValid(jitterTexels);
			taa["subPixels"].SetIfValid(subPixels);
			taa["resampling"].SetIfValid(args.renderSize == outputSize ? 0.0f : 1.0f);
			taa["sampleWeightK"].SetIfValid(
				1.0f / (2.0f * args.reconstructionWidth * args.reconstructionWidth));
			taa["blendWeight"].SetIfValid(c_BlendWeight);
			taa["historyValid"].SetIfValid(args.historyValid ? 1.0f : 0.0f);
		}
		else
		{
			gfatal("TaaResolve shader is missing its '{}' constant buffer", c_Cbuffer);
		}

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(args.viewport);
		gfxState.frameBuffer = FrameBuffer().AddColorAttachment(args.history);

		cmd->SetMeshletState(gfxState);

		cmd->DispatchMesh(1, 1, 1);
	}
}
