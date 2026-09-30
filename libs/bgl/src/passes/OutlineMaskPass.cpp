// A SharedRef<ISceneView> is dereferenced and destroyed here, both of which need the
// complete type -- include-cleaner sees only the declaration.
#include "passes/OutlineMaskPass.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "gfx/frame_constants.h"
#include "passes/DrawData.h"
#include "passes/SceneBindings.h"
#include "scene/scene_buffer_names.h"
#include <bgl/ISceneView.h>  // IWYU pragma: keep
#include <bgl/idl/BaseTable.h>
#include <bgl/idl/LodDrawMode.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/constants/constants.h>
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
#include <core/err/util.h>

// The exec lambda copies DrawData, whose SceneViewRef needs the complete type to destroy.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		// The tier-branching stage, so a selected rig or crowd contours the shape it is posed in
		// rather than the bind pose its vertex bytes hold. A selection mixes tiers freely, and this
		// pass dispatches its whole list at once.
		constexpr auto c_GeomSrc  = "programs.forward.AnyMesh"sv;
		constexpr auto c_PixelSrc = "programs.screen.OutlineMask"sv;

		constexpr auto c_MaskFormat = bgpu::Format::R8_UNORM;
	}

	void
	OutlineMaskPass::Init(const PassInitContext& ctx)
	{
		core::ensure(ctx.device != nullptr, "Device must be initialized");

		auto pipelineDesc = bgpu::MeshletPipelineDesc();

		pipelineDesc.ampShader   = ctx.device->CreateShader(std::string(c_GeomSrc), "ASMain");
		pipelineDesc.meshShader  = ctx.device->CreateShader(std::string(c_GeomSrc), "MSMain");
		pipelineDesc.pixelShader = ctx.device->CreateShader(std::string(c_PixelSrc), "PSMain");

		pipelineDesc.AddRtvFormat(c_MaskFormat);

		// No depth attachment and no culling: the mask is the full silhouette, occluded or not,
		// whichever way its triangles face.
		auto raster = bgpu::RasterState();
		raster.SetFillMode(bgpu::RasterFillMode::kSolid)
			.SetCullMode(bgpu::RasterCullMode::kNone)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(true);

		auto depth = bgpu::DepthStencilState{};
		depth.SetDepthTestEnable(false).SetDepthWriteEnable(false).SetStencilEnable(false);

		pipelineDesc.renderState =
			bgpu::RenderState().SetRasterState(raster).SetDepthStencilState(depth);

		ctx.pipelines->Add(m_Kernel, std::move(pipelineDesc));
	}

	void
	OutlineMaskPass::AttachToFrameGraph(
		FrameGraph&     fg,
		const DrawData& draw,
		uint32_t        selectedCount)
	{
		core::ensure(selectedCount > 0, "An empty selection attaches no mask pass");

		auto desc = PassDesc();

		desc.SetName("Outline Mask {}", draw.drawIdx)
			.AddRenderTarget(c_OutlineMaskName)
			.AddBufferArg(
				c_SelectedInstancesName,
				bgpu::BarrierSyncFlag::kVertexShader,
				bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddBufferArg(
				c_InstanceLodName,
				bgpu::BarrierSyncFlag::kVertexShader,
				bgpu::BarrierAccessFlag::kUnorderedAccess);

		for (const std::span<const SceneBuffer> bindings :
		     { std::span<const SceneBuffer>(c_ForwardDataBuffers),
		       std::span<const SceneBuffer>(c_SkinnedBuffers) })
		{
			for (const SceneBuffer& binding : bindings)
			{
				desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
			}
		}

		desc.SetExec([this, draw, selectedCount](const PassContext& resources) {
			Execute(draw, selectedCount, resources);
		});

		fg.AddPass(std::move(desc));
	}

	void
	OutlineMaskPass::Execute(
		const DrawData&    draw,
		uint32_t           selectedCount,
		const PassContext& resources)
	{
		bgpu::ICommandList* cmd = resources.GetCommandList();

		core::ensure(cmd != nullptr, "Pass commandlist must be initialized");
		core::ensure(
			m_Kernel.pipeline.IsInitialized(),
			"Outline mask pipeline must be initialized");

		if (auto foundForwardData = m_Kernel.FindUniforms("forwardData"))
		{
			BindSceneBuffers(*foundForwardData, c_ForwardDataBuffers, resources);
		}

		if (auto foundSkinnedData = m_Kernel.FindUniforms("skinnedData"))
		{
			BindSceneBuffers(*foundSkinnedData, c_SkinnedBuffers, resources);
		}

		if (auto foundViewData = m_Kernel.FindUniforms("viewData"))
		{
			auto& viewData = *foundViewData;

			// The mask is consumed after the TAA resolve and never accumulated, so it must not
			// carry the sample offset -- a jittered contour shimmers by half a pixel.
			viewData["viewProj"]     = draw.viewState.unjitteredViewProj;
			viewData["prevViewProj"] = draw.viewState.unjitteredViewProj;
			viewData["jitter"]       = glm::vec2(0.0f);
			viewData["prevJitter"]   = glm::vec2(0.0f);

			// Both the same clock, like the matrices: the mask has no motion vector to feed. An
			// animated instance still poses at `time`, so its contour follows the pose the forward
			// pass drew.
			viewData["time"]     = draw.clock.time;
			viewData["prevTime"] = draw.clock.time;
		}

		if (auto foundExpansion = m_Kernel.FindUniforms("expansionData"))
		{
			auto& expansion = *foundExpansion;

			const auto selected = resources.GetBuffer(c_SelectedInstancesName);

			// kDepthSorted starts the list at zero, exactly like the transparent phase; the
			// prefix-sum key is never read under it, and is bound only so it holds a live handle.
			expansion["compactedInstances"]  = selected;
			expansion["drawBucketPrefixSum"] = selected;
			expansion["baseTable"]           = idl::BaseTable::kDepthSorted;
			expansion["drawLane"]            = 0u;
			// The mask is the whole silhouette whichever way its triangles face, and this pass
			// binds no material for the mesh stage to consult.
			expansion["cullBackfaces"] = 0u;
			// The level the forward pass drew, so the contour hugs the geometry on screen. A
			// dissolve's outgoing level is not traced: the selected list carries no outgoing entry.
			expansion["instanceLod"] = resources.GetBuffer(c_InstanceLodName);
			expansion["lodDrawMode"] = idl::LodDrawMode::kCurrent;
		}

		auto gfxState   = bgpu::MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		gfxState.frameBuffer = bgpu::FrameBuffer().AddColorAttachment(draw.targets.outlineMask);

		cmd->SetMeshletState(gfxState);

		// One amplification group per selected drawable; the count is CPU state, so nothing is
		// indirect.
		cmd->DispatchMesh(selectedCount, 1, 1);
	}
}
