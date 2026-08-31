#include "passes/OutlineMaskPass.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "idl/BaseTable.h"
#include "passes/DrawData.h"
#include "passes/SceneBindings.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/FrameBuffer.h"
#include "resource/Shader.h"
#include "types/RenderState.h"

// The exec lambda copies DrawData, whose SceneViewRef needs the complete type to destroy.
#include <bgl/ISceneView.h>

namespace bgl
{
	namespace
	{
		// The tier-branching stage, so a selected rig or crowd contours the shape it is posed in
		// rather than the bind pose its vertex bytes hold. A selection mixes tiers freely, and this
		// pass dispatches its whole list at once.
		constexpr auto c_GeomSrc  = "programs.forward.AnyMesh"sv;
		constexpr auto c_PixelSrc = "programs.screen.OutlineMask"sv;

		constexpr auto c_MaskFormat = Format::R8_UNORM;
	}

	void
	OutlineMaskPass::Init(IDevice* device)
	{
		gassert(device != nullptr, "Device must be initialized");

		auto pipelineDesc = MeshletPipelineDesc();

		pipelineDesc.ampShader   = device->CreateShader(std::string(c_GeomSrc), "ASMain");
		pipelineDesc.meshShader  = device->CreateShader(std::string(c_GeomSrc), "MSMain");
		pipelineDesc.pixelShader = device->CreateShader(std::string(c_PixelSrc), "PSMain");

		pipelineDesc.AddRtvFormat(c_MaskFormat);

		// No depth attachment and no culling: the mask is the full silhouette, occluded or not,
		// whichever way its triangles face.
		auto raster = RasterState();
		raster.SetFillMode(RasterFillMode::kSolid)
			.SetCullMode(RasterCullMode::kNone)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(true);

		auto depth = DepthStencilState{};
		depth.SetDepthTestEnable(false).SetDepthWriteEnable(false).SetStencilEnable(false);

		pipelineDesc.renderState = RenderState().SetRasterState(raster).SetDepthStencilState(depth);

		m_Kernel = device->CreateMeshletKernel(pipelineDesc);
	}

	void
	OutlineMaskPass::AttachToFrameGraph(
		FrameGraph&     fg,
		const DrawData& draw,
		uint32_t        selectedCount)
	{
		gassert(selectedCount > 0, "An empty selection attaches no mask pass");

		auto desc = PassDesc();

		desc.SetName("Outline Mask {}", draw.drawIdx)
			.AddTextureArg(
				TextureArg{ std::string(c_OutlineMaskName),
		                    BarrierSyncFlag::kRenderTarget,
		                    BarrierAccessFlag::kRenderTarget,
		                    BarrierLayout::kRenderTarget })
			.AddBufferArg(
				BufferArg{ std::string(c_SelectedInstancesName),
		                   BarrierSyncFlag::kVertexShader,
		                   BarrierAccessFlag::kShaderResource });

		for (const std::span<const SceneBuffer> bindings :
		     { std::span<const SceneBuffer>(c_ForwardDataBuffers),
		       std::span<const SceneBuffer>(c_SkinnedBuffers),
		       std::span<const SceneBuffer>(c_VatBuffers) })
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
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(m_Kernel.pipeline.IsInitialized(), "Outline mask pipeline must be initialized");

		if (auto foundForwardData = m_Kernel.FindUniforms("forwardData"))
		{
			BindSceneBuffers(*foundForwardData, c_ForwardDataBuffers, resources);
		}

		if (auto foundSkinnedData = m_Kernel.FindUniforms("skinnedData"))
		{
			BindSceneBuffers(*foundSkinnedData, c_SkinnedBuffers, resources);
		}

		if (auto foundVatData = m_Kernel.FindUniforms("vatData"))
		{
			BindSceneBuffers(*foundVatData, c_VatBuffers, resources);
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
			expansion["compactedInstances"] = selected;
			expansion["psoPrefixSum"]       = selected;
			expansion["baseTable"]          = idl::BaseTable::kDepthSorted;
			expansion["psoIndex"]           = 0u;
		}

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		gfxState.frameBuffer = FrameBuffer().AddColorAttachment(draw.targets.outlineMask);

		cmd->SetMeshletState(gfxState);

		// One amplification group per selected drawable; the count is CPU state, so nothing is
		// indirect.
		cmd->DispatchMesh(selectedCount, 1, 1);
	}
}
