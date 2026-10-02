#include "passes/BlobShadowPass.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/frame_constants.h"
#include "passes/BindingNameCheck.h"
#include "passes/DrawData.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "scene/scene_buffer_names.h"
#include <array>
#include <bgl/ISceneView.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/constants/constants.h>
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/pipeline/PipelineBatch.h>
#include <bgpu/resource/FrameBuffer.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/BlendState.h>
#include <bgpu/types/DepthStencilState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/MeshletState.h>
#include <bgpu/types/RasterState.h>
#include <bgpu/types/RenderState.h>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "programs.forward.BlobShadow"sv;

		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in BlobShadow.slang.
		constexpr auto c_Cbuffer = "gBlobShadowData"sv;

		// Every member Draw writes, kept beside the code that writes them so BindingNameCheck catches
		// a shader rename at startup.
		constexpr std::array<std::string_view, 10> c_Fields = {
			"blobBuffer"sv, "meshBuffer"sv, "palettes"sv,    "playbackBuffer"sv, "rigs"sv,
			"worldDepth"sv, "viewProj"sv,   "invViewProj"sv, "groundNormal"sv,   "viewportRect"sv,
		};
	}

	BlobShadowPass::BlobShadowPass(const PassInitContext& ctx)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		// The transparents' blend, but colour only with no depth attachment -- the depth is this
		// pass's input -- and its own two-stage program: the discs are not instance-pipeline
		// geometry.
		auto pipelineDesc = bgpu::MeshletPipelineDesc();

		pipelineDesc.meshShader  = ctx.device->CreateShader(std::string(c_Src), "MSMain");
		pipelineDesc.pixelShader = ctx.device->CreateShader(std::string(c_Src), "PSMain");

		pipelineDesc.AddRtvFormat(bgpu::Format::RGBA16_FLOAT);

		auto raster = bgpu::RasterState();
		raster.SetFillMode(bgpu::RasterFillMode::kSolid)
			.SetCullMode(bgpu::RasterCullMode::kNone)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(true);

		// No test: the depth holds the world alone, so the receiver each fragment reads is exactly
		// what the pixel shows. A unit in front is drawn after, and covers the decal.
		auto depth = bgpu::DepthStencilState{};
		depth.SetDepthTestEnable(false).SetDepthWriteEnable(false).SetStencilEnable(false);

		auto blend = bgpu::BlendState{};
		blend.SetRenderTarget(
			0,
			bgpu::BlendState::RenderTarget{}
				.EnableBlend()
				.SetSrcBlend(bgpu::BlendFactor::kOne)
				.SetDestBlend(bgpu::BlendFactor::kInvSrcAlpha)
				.SetBlendOp(bgpu::BlendOp::kAdd)
				.SetSrcBlendAlpha(bgpu::BlendFactor::kZero)
				.SetDestBlendAlpha(bgpu::BlendFactor::kZero)
				.SetBlendOpAlpha(bgpu::BlendOp::kAdd));

		pipelineDesc.renderState =
			bgpu::RenderState().SetRasterState(raster).SetBlendState(blend).SetDepthStencilState(
				depth);

		ctx.pipelines->Add(m_Kernel, std::move(pipelineDesc));
	}

	void
	BlobShadowPass::CheckBindings() const
	{
		BindingNameCheck("BlobShadowPass"sv, { &m_Kernel, 1 }).Check(c_Cbuffer, c_Fields);
	}

	void
	BlobShadowPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		const auto* view = draw.view->As<SceneView>();
		core::ensure(view != nullptr, "BlobShadowPass requires a bgl::SceneView");

		if (view->GetBlobShadowCount() == 0)
		{
			return;
		}

		auto desc = PassDesc();

		desc.SetName("Blob Shadows {}", draw.drawIdx)
			.AddRenderTarget(c_BackbufferName)
			.AddTextureRead(c_DepthName, bgpu::BarrierSyncFlag::kPixelShader)
			.AddBufferRead(c_BlobShadowsName, bgpu::BarrierSyncFlag::kVertexShader)
			.AddBufferRead(c_MeshInstanceBufferName, bgpu::BarrierSyncFlag::kVertexShader)
			.AddBufferRead(c_BonePaletteName, bgpu::BarrierSyncFlag::kVertexShader)
			.AddBufferRead("scene.playbackBuffer", bgpu::BarrierSyncFlag::kVertexShader)
			.AddBufferRead("scene.rigBuffer", bgpu::BarrierSyncFlag::kVertexShader);

		desc.SetExec([this, draw](const PassContext& resources) { Execute(draw, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	BlobShadowPass::Execute(const DrawData& draw, const PassContext& resources)
	{
		const auto* view = draw.view->As<SceneView>();

		const uint32_t blobs = view->GetBlobShadowCount();

		core::ensure(m_Kernel.pipeline.IsInitialized(), "Blob shadow pipeline must be initialized");

		if (auto found = m_Kernel.FindUniforms(c_Cbuffer))
		{
			auto& uniforms = *found;

			uniforms["blobBuffer"]     = resources.GetBuffer(c_BlobShadowsName);
			uniforms["meshBuffer"]     = resources.GetBuffer(c_MeshInstanceBufferName);
			uniforms["palettes"]       = resources.GetBuffer(c_BonePaletteName);
			uniforms["playbackBuffer"] = resources.GetBuffer("scene.playbackBuffer");
			uniforms["rigs"]           = resources.GetBuffer("scene.rigBuffer");
			uniforms["worldDepth"].SetIfValid(draw.targets.depthSrv);
			uniforms["viewProj"]    = draw.viewState.viewProj;
			uniforms["invViewProj"] = glm::inverse(draw.viewState.viewProj);

			const GroundPlaneDesc& ground = view->GetScene()->As<Scene>()->GetGround();
			uniforms["groundNormal"]      = ground.normal;

			const bgpu::Viewport& viewport = draw.viewState.viewport;
			uniforms["viewportRect"]       = glm::vec4(
				viewport.minX,
				viewport.minY,
				1.0f / (viewport.maxX - viewport.minX),
				1.0f / (viewport.maxY - viewport.minY));
		}
		else
		{
			core::fatal("Blob shadow shader is missing its '{}' constant buffer", c_Cbuffer);
		}

		// Colour alone: the velocity buffer is not the decal's to write, and the depth is read.
		auto gfxState = bgpu::MeshletState();
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		gfxState.frameBuffer = bgpu::FrameBuffer().AddColorAttachment(draw.targets.sceneColor);
		gfxState.kernel      = &m_Kernel;

		bgpu::ICommandList* cmd = resources.GetCommandList();
		cmd->SetMeshletState(gfxState);
		cmd->DispatchMesh(blobs, 1, 1);
	}
}
