#include "passes/BoneOverlayPass.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "passes/DrawData.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/FrameBuffer.h"
#include "resource/Shader.h"
#include "scene/SceneView.h"
#include "scene/scene_buffer_names.h"
#include "types/RenderState.h"
#include "uniforms/Uniforms.h"

// The exec lambda copies DrawData, whose SceneViewRef needs the complete type to destroy.
#include <bgl/ISceneView.h>

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "BoneOverlay"sv;

		constexpr auto c_OverlayFormat = Format::RGBA8_UNORM;

		// World units for a bone with no length to be drawn along -- a root, or one standing on its
		// parent. Small enough not to swamp a hand's joints, large enough to see on a whole rig.
		constexpr float c_RootSize = 0.08f;

		const auto c_Color = glm::vec3(0.35f, 0.78f, 1.0f);
	}

	void
	BoneOverlayPass::Init(IDevice* device)
	{
		gassert(device != nullptr, "Device must be initialized");

		auto pipelineDesc = MeshletPipelineDesc();

		pipelineDesc.meshShader  = device->CreateShader(std::string(c_Src), "MSMain");
		pipelineDesc.pixelShader = device->CreateShader(std::string(c_Src), "PSMain");

		pipelineDesc.AddRtvFormat(c_OverlayFormat);
		pipelineDesc.SetDsvFormat(Format::D24S8);

		// Solids, back faces dropped: an octahedron is convex, so its front faces are the whole of
		// what should be seen of it.
		auto raster = RasterState();
		raster.SetFillMode(RasterFillMode::kSolid)
			.SetCullMode(RasterCullMode::kBack)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(true);

		// Against the overlay's own depth, never the scene's: the bones sort among themselves, and
		// the skin they sit inside cannot hide them.
		auto depth = DepthStencilState{};
		depth.SetDepthTestEnable(true)
			.SetDepthWriteEnable(true)
			.SetDepthFunc(ComparisonFunc::kLessOrEqual)
			.SetStencilEnable(false);

		pipelineDesc.renderState = RenderState().SetRasterState(raster).SetDepthStencilState(depth);

		m_Kernel = device->CreateMeshletKernel(pipelineDesc);
	}

	void
	BoneOverlayPass::Release()
	{
		logger::trace("BoneOverlayPass::Release");
		m_Kernel.Reset();
	}

	void
	BoneOverlayPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		auto desc = PassDesc();

		desc.SetName("Bone Overlay {}", draw.drawIdx)
			.AddTextureArg(
				TextureArg{ std::string(c_BoneOverlayName),
		                    BarrierSyncFlag::kRenderTarget,
		                    BarrierAccessFlag::kRenderTarget,
		                    BarrierLayout::kRenderTarget })
			.AddTextureArg(
				TextureArg{ std::string(c_BoneOverlayDepthName),
		                    BarrierSyncFlag::kDepthStencil,
		                    BarrierAccessFlag::kDepthWrite,
		                    BarrierLayout::kDepthWrite })
			.AddBufferArg(
				c_PosedInstancesName,
				BarrierSyncFlag::kVertexShader,
				BarrierAccessFlag::kShaderResource)
			.AddBufferArg(
				c_PosedMeshesName,
				BarrierSyncFlag::kVertexShader,
				BarrierAccessFlag::kShaderResource)
			.AddBufferArg(
				c_MeshInstanceBufferName,
				BarrierSyncFlag::kVertexShader,
				BarrierAccessFlag::kShaderResource)
			.AddBufferArg(
				c_SkinnedStateBufferName,
				BarrierSyncFlag::kVertexShader,
				BarrierAccessFlag::kShaderResource)
			.AddBufferArg(
				c_SkinnedGeomBufferName,
				BarrierSyncFlag::kVertexShader,
				BarrierAccessFlag::kShaderResource)
			.AddBufferArg(
				c_SkinnedBoneBufferName,
				BarrierSyncFlag::kVertexShader,
				BarrierAccessFlag::kShaderResource)
			.AddBufferArg(
				c_BonePaletteName,
				BarrierSyncFlag::kVertexShader,
				BarrierAccessFlag::kShaderResource);

		desc.SetExec([this, draw](const PassContext& ctx) { Execute(draw, ctx); });

		fg.AddPass(std::move(desc));
	}

	void
	BoneOverlayPass::Execute(const DrawData& draw, const PassContext& ctx)
	{
		ICommandList* cmd = ctx.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(m_Kernel.pipeline.IsInitialized(), "Bone overlay pipeline must be initialized");

		const auto* view = draw.view->As<SceneView>();
		gassert(view != nullptr, "BoneOverlayPass requires a bgl::SceneView");

		const uint32_t posed    = view->GetPosedInstanceCount();
		const uint32_t maxBones = view->GetMaxPosedBoneCount();

		if (posed == 0 || maxBones == 0)
		{
			return;
		}

		if (auto found = m_Kernel.FindUniforms("gUniforms"))
		{
			auto& uniforms = *found;

			uniforms["posedInstances"] = ctx.GetBuffer(c_PosedInstancesName);
			uniforms["posedMeshes"]    = ctx.GetBuffer(c_PosedMeshesName);
			uniforms["meshes"]         = ctx.GetBuffer(c_MeshInstanceBufferName);
			uniforms["skinnedStates"]  = ctx.GetBuffer(c_SkinnedStateBufferName);
			uniforms["skinnedGeoms"]   = ctx.GetBuffer(c_SkinnedGeomBufferName);
			uniforms["boneBuffer"]     = ctx.GetBuffer(c_SkinnedBoneBufferName);
			uniforms["bonePalettes"]   = ctx.GetBuffer(c_BonePaletteName);

			// Unjittered, like the outline mask: the overlay is composited after the resolve and
			// never accumulated, so a jittered skeleton would shimmer by half a pixel.
			uniforms["viewProj"] = draw.viewState.unjitteredViewProj;
			uniforms["rootSize"] = c_RootSize;
			uniforms["color"]    = c_Color;
		}
		else
		{
			gfatal("Bone overlay shader is missing its 'gUniforms' constant buffer");
		}

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewState.outputViewport);
		gfxState.frameBuffer = FrameBuffer()
		                           .AddColorAttachment(draw.targets.boneOverlay)
		                           .SetDepthAttachment(draw.targets.boneOverlayDepth);

		cmd->SetMeshletState(gfxState);

		// One group per bone of the widest rig, per posed instance. A narrower rig's surplus groups
		// stop at their own bone count, which is cheaper than the prefix sum that would size this
		// exactly.
		cmd->DispatchMesh(maxBones, posed, 1);
	}
}
