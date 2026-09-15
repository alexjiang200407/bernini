#include "passes/StaticDepthPass.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "passes/BinderNames.h"
#include "passes/DrawData.h"
#include "passes/SceneBindings.h"
#include "pipeline/MeshletKernel.h"
#include "pipeline/MeshletPipeline.h"
#include "pipeline/PipelineBatch.h"
#include "resource/FrameBuffer.h"
#include "resource/Shader.h"
#include "scene/scene_buffer_names.h"
#include "types/Barrier.h"
#include "types/DepthStencilState.h"
#include "types/Format.h"
#include "types/MeshletState.h"
#include "types/RasterState.h"
#include "types/RenderState.h"
#include "util/util.h"
#include <array>
#include <bgl/ISceneView.h>
#include <bgl/MaterialType.h>
#include <bgl_common/gassert.h>
#include <bgl_common/idl/BaseTable.h>
#include <bgl_common/idl/PsoType.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace bgl
{
	namespace
	{
		constexpr auto c_GeomSrc  = "programs.forward.StaticMesh"sv;
		constexpr auto c_PixelSrc = "programs.forward.DepthOnly"sv;

		constexpr std::array<std::string_view, 6> c_ViewDataFields = {
			"viewProj"sv, "prevViewProj"sv, "jitter"sv, "prevJitter"sv, "time"sv, "prevTime"sv,
		};

		constexpr std::array<std::string_view, 4> c_ExpansionDataFields = {
			"psoIndex"sv,
			"baseTable"sv,
			"compactedInstances"sv,
			"cullBackfaces"sv,
		};

		// The buckets whose depth is a receiver's: every static row that writes depth
		// unconditionally. The cutout and hashed static rows are absent until their coverage is
		// evaluated here with the colour pass's own seed -- a receiver that ignored coverage would
		// catch shadows on discarded texels.
		std::array<uint16_t, 4 + cGameSlots>
		StaticOpaquePsos() noexcept
		{
			std::array<uint16_t, 4 + cGameSlots> psos = {
				static_cast<uint16_t>(idl::PsoType::kOpaque_StaticMesh_Null),
				static_cast<uint16_t>(idl::PsoType::kOpaque_StaticMesh_PBR),
				static_cast<uint16_t>(idl::PsoType::kOpaque_StaticMesh_LoosePbr),
				static_cast<uint16_t>(idl::PsoType::kAssert_StaticMesh),
			};

			for (uint32_t slot = 0; slot < cGameSlots; ++slot)
			{
				psos[4 + slot] = static_cast<uint16_t>(GameSlotRowBase(slot));
			}

			return psos;
		}
	}

	void
	StaticDepthPass::Init(IDevice* device, PipelineBatch& pipelines)
	{
		gassert(device != nullptr, "Device must be initialized");

		// The forward static geometry stage over a depth-only pixel stage, with no colour
		// attachment: one pipeline serves every opaque static bucket, since opaque depth does not
		// depend on the material.
		auto pipelineDesc = MeshletPipelineDesc();

		pipelineDesc.ampShader   = device->CreateShader(std::string(c_GeomSrc), "ASMain");
		pipelineDesc.meshShader  = device->CreateShader(std::string(c_GeomSrc), "MSMain");
		pipelineDesc.pixelShader = device->CreateShader(std::string(c_PixelSrc), "PSMain");

		pipelineDesc.SetDsvFormat(Format::D24S8);

		auto raster = RasterState();
		raster.SetFillMode(RasterFillMode::kSolid)
			.SetCullMode(RasterCullMode::kBack)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(true);

		auto depth = DepthStencilState{};
		depth.SetDepthTestEnable(true)
			.SetDepthWriteEnable(true)
			.SetDepthFunc(ComparisonFunc::kLess)
			.SetStencilEnable(false);

		pipelineDesc.renderState = RenderState().SetRasterState(raster).SetDepthStencilState(depth);

		pipelines.Add(m_Kernel, std::move(pipelineDesc));
	}

	void
	StaticDepthPass::CheckBindings() const
	{
		BinderNames("StaticDepthPass"sv, { &m_Kernel, 1 })
			.Check("forwardData"sv, GetUniformKeys(c_ForwardDataBuffers))
			.Check("expansionData"sv, GetUniformKeys(c_ExpansionBuffers))
			.Check("expansionData"sv, c_ExpansionDataFields)
			.Check("viewData"sv, c_ViewDataFields);
	}

	void
	StaticDepthPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		auto desc = PassDesc();

		desc.SetName("StaticDepth {}", draw.drawIdx)
			.AddTextureArg(
				TextureArg{ std::string(c_StaticDepthName),
		                    BarrierSyncFlag::kDepthStencil,
		                    BarrierAccessFlag::kDepthWrite,
		                    BarrierLayout::kDepthWrite })
			.AddBufferArg(
				BufferArg{ std::string(c_CompactDispatchArgsName),
		                   BarrierSyncFlag::kIndirectArgument,
		                   BarrierAccessFlag::kIndirectArgument });

		for (const auto& binding : c_ForwardDataBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		for (const auto& binding : c_ExpansionBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		desc.SetExec([this, draw](const PassContext& resources) { Execute(draw, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	StaticDepthPass::Execute(const DrawData& draw, const PassContext& resources)
	{
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(m_Kernel.pipeline.IsInitialized(), "Static depth pipeline must be initialized");

		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		if (auto foundForwardData = m_Kernel.FindUniforms("forwardData"))
		{
			BindSceneBuffers(*foundForwardData, c_ForwardDataBuffers, resources);
		}

		if (auto foundViewData = m_Kernel.FindUniforms("viewData"))
		{
			auto& viewData           = *foundViewData;
			viewData["viewProj"]     = draw.viewState.viewProj;
			viewData["prevViewProj"] = draw.viewState.prevViewProj;
			viewData["jitter"]       = draw.viewState.jitter;
			viewData["prevJitter"]   = draw.viewState.prevJitter;
			viewData["time"]         = draw.clock.time;
			viewData["prevTime"]     = draw.clock.prevTime;
		}

		auto gfxState = MeshletState();
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		gfxState.frameBuffer  = FrameBuffer().SetDepthAttachment(draw.targets.staticDepth);
		gfxState.kernel       = &m_Kernel;
		gfxState.indirectArgs = resources.GetBuffer(c_CompactDispatchArgsName);

		auto expansionData = m_Kernel.FindUniforms("expansionData");
		if (expansionData)
		{
			BindSceneBuffers(*expansionData, c_ExpansionBuffers, resources);
			(*expansionData)["baseTable"]     = idl::BaseTable::kPsoBucketed;
			(*expansionData)["cullBackfaces"] = 0u;
		}

		// One pipeline, one bucket per dispatch: the cbuffer is re-uploaded on every dispatch, so
		// rewriting psoIndex between them is sound (docs/uniforms.md).
		for (const uint16_t pso : StaticOpaquePsos())
		{
			if (expansionData)
			{
				(*expansionData)["psoIndex"] = static_cast<uint32_t>(pso);
			}

			cmd->SetMeshletState(gfxState);
			cmd->DispatchMeshIndirect(pso);
		}
	}
}
