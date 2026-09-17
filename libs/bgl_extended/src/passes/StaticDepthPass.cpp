#include "passes/StaticDepthPass.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "passes/BindingNameCheck.h"
#include "passes/DrawData.h"
#include "passes/ForwardPass.h"
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

		// What a discard-only coverage stage actually reads of MaterialData: the arena's samplers
		// and the seed. The lighting fields stay unbound -- coverage shades nothing.
		constexpr std::array<std::string_view, 2> c_MaterialDataFields = {
			"anisoLinearWrapSampler"sv,
			"alphaHashSeed"sv,
		};

		// The buckets whose depth is a receiver's and needs no coverage: every static row that
		// writes depth unconditionally.
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

		// The static cutout and hashed rows, each with the discard-only twin of its colour-pass
		// pixel stage: coverage is evaluated here with the colour pass's own seed, or the receiver
		// would catch shadows on discarded texels. Order matches m_CoverageKernels.
		struct CoverageBucket
		{
			std::string_view pixelSrc;
			uint16_t         pso;
		};

		std::array<CoverageBucket, 4 + 2 * cGameSlots>
		StaticCoverageBuckets() noexcept
		{
			std::array<CoverageBucket, 4 + 2 * cGameSlots> buckets = { {
				{ "programs.forward.DepthOnly_PBR_AlphaTest"sv,
				  static_cast<uint16_t>(idl::PsoType::kAlphaTest_StaticMesh_PBR) },
				{ "programs.forward.DepthOnly_PBR_Loose_AlphaTest"sv,
				  static_cast<uint16_t>(idl::PsoType::kAlphaTest_StaticMesh_LoosePbr) },
				{ "programs.forward.DepthOnly_PBR_HashedAlpha"sv,
				  static_cast<uint16_t>(idl::PsoType::kHashedAlpha_StaticMesh_PBR) },
				{ "programs.forward.DepthOnly_PBR_Loose_HashedAlpha"sv,
				  static_cast<uint16_t>(idl::PsoType::kHashedAlpha_StaticMesh_LoosePbr) },
			} };

			constexpr std::array<std::string_view, cGameSlots> c_CutoutSrcs = {
				"programs.forward.DepthOnly_GameSlot0_AlphaTest"sv,
				"programs.forward.DepthOnly_GameSlot1_AlphaTest"sv,
				"programs.forward.DepthOnly_GameSlot2_AlphaTest"sv,
				"programs.forward.DepthOnly_GameSlot3_AlphaTest"sv,
			};
			constexpr std::array<std::string_view, cGameSlots> c_HashedSrcs = {
				"programs.forward.DepthOnly_GameSlot0_HashedAlpha"sv,
				"programs.forward.DepthOnly_GameSlot1_HashedAlpha"sv,
				"programs.forward.DepthOnly_GameSlot2_HashedAlpha"sv,
				"programs.forward.DepthOnly_GameSlot3_HashedAlpha"sv,
			};

			// The static tier's rows sit at the slot base: opaque, then cutout, then hashed
			// (ForwardPass::MakePsos fixes the order).
			for (uint32_t slot = 0; slot < cGameSlots; ++slot)
			{
				const auto base       = static_cast<uint16_t>(GameSlotRowBase(slot));
				buckets[4 + slot * 2] = { c_CutoutSrcs[slot], static_cast<uint16_t>(base + 1) };
				buckets[5 + slot * 2] = { c_HashedSrcs[slot], static_cast<uint16_t>(base + 2) };
			}

			return buckets;
		}

		// The mesh stage's half of ForwardPass's culling: a row the hardware does not cull hands
		// back faces to the material's doubleSided, exactly as ForwardPass::Execute binds it.
		[[nodiscard]] uint32_t
		MeshStageCullsBackfaces(const uint16_t pso) noexcept
		{
			return ForwardPass::PsoCullMode(pso) == RasterCullMode::kNone ? 1u : 0u;
		}
	}

	namespace
	{
		MeshletPipelineDesc
		DepthPipelineDesc(
			IDevice*               device,
			const std::string_view pixelSrc,
			const RasterCullMode   cull)
		{
			auto pipelineDesc = MeshletPipelineDesc();

			pipelineDesc.ampShader   = device->CreateShader(std::string(c_GeomSrc), "ASMain");
			pipelineDesc.meshShader  = device->CreateShader(std::string(c_GeomSrc), "MSMain");
			pipelineDesc.pixelShader = device->CreateShader(std::string(pixelSrc), "PSMain");

			pipelineDesc.SetDsvFormat(Format::D24S8);

			auto raster = RasterState();
			raster.SetFillMode(RasterFillMode::kSolid)
				.SetCullMode(cull)
				.SetFrontCounterClockwise(true)
				.SetDepthClipEnable(true);

			auto depth = DepthStencilState{};
			depth.SetDepthTestEnable(true)
				.SetDepthWriteEnable(true)
				.SetDepthFunc(ComparisonFunc::kLess)
				.SetStencilEnable(false);

			pipelineDesc.renderState =
				RenderState().SetRasterState(raster).SetDepthStencilState(depth);

			return pipelineDesc;
		}
	}

	void
	StaticDepthPass::Init(IDevice* device, PipelineBatch& pipelines)
	{
		gassert(device != nullptr, "Device must be initialized");

		// Opaque depth does not depend on the material, so the opaque rows share a depth-only pixel
		// stage and differ only in where back faces are culled.
		pipelines.Add(
			m_HardwareCullKernel,
			DepthPipelineDesc(device, c_PixelSrc, RasterCullMode::kBack));
		pipelines.Add(
			m_MaterialCullKernel,
			DepthPipelineDesc(device, c_PixelSrc, RasterCullMode::kNone));
	}

	void
	StaticDepthPass::AddBucketKernels(
		IDevice*          device,
		PipelineBatch&    pipelines,
		const BucketMask& buckets)
	{
		gassert(device != nullptr, "Device must be initialized");

		const auto coverageBuckets = StaticCoverageBuckets();
		for (size_t i = 0; i < coverageBuckets.size(); ++i)
		{
			if (buckets.test(coverageBuckets[i].pso) &&
			    !m_CoverageKernels[i].pipeline.IsInitialized())
			{
				pipelines.Add(
					m_CoverageKernels[i],
					DepthPipelineDesc(
						device,
						coverageBuckets[i].pixelSrc,
						ForwardPass::PsoCullMode(coverageBuckets[i].pso)));
			}
		}
	}

	void
	StaticDepthPass::CheckBindings() const
	{
		// The opaque kernels read the material arena for doubleSided alone.
		const std::array<const MeshletKernel*, 2> opaque = { &m_HardwareCullKernel,
			                                                 &m_MaterialCullKernel };
		for (const MeshletKernel* kernel : opaque)
		{
			BindingNameCheck("StaticDepthPass"sv, { kernel, 1 })
				.Check("forwardData"sv, GetUniformKeys(c_ForwardDataBuffers))
				.Check("expansionData"sv, GetUniformKeys(c_ExpansionBuffers))
				.Check("expansionData"sv, c_ExpansionDataFields)
				.Check("viewData"sv, c_ViewDataFields)
				.Check("materialData"sv, GetUniformKeys(c_MaterialBuffers));
		}

		// The coverage family is demand-built; nothing to read names off until a first bucket is,
		// and EnsureBucketPipelines re-checks after every build.
		if (!AnyInitialized(m_CoverageKernels))
		{
			return;
		}

		BindingNameCheck(
			"StaticDepthPass"sv,
			{ m_CoverageKernels.data(), m_CoverageKernels.size() })
			.Check("forwardData"sv, GetUniformKeys(c_ForwardDataBuffers))
			.Check("expansionData"sv, GetUniformKeys(c_ExpansionBuffers))
			.Check("expansionData"sv, c_ExpansionDataFields)
			.Check("viewData"sv, c_ViewDataFields)
			.Check("materialData"sv, GetUniformKeys(c_MaterialBuffers))
			.Check("materialData"sv, c_MaterialDataFields);
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

		// The material arena, for doubleSided and the coverage stages' records and textures.
		// Declared so its barriers are placed; the typed view itself is bound off the draw (see
		// ForwardPass).
		for (const auto& binding : c_MaterialBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		desc.SetExec([this, draw](const PassContext& resources) { Execute(draw, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	StaticDepthPass::BindKernel(
		MeshletKernel&     kernel,
		const DrawData&    draw,
		const PassContext& resources)
	{
		if (auto foundForwardData = kernel.FindUniforms("forwardData"))
		{
			BindSceneBuffers(*foundForwardData, c_ForwardDataBuffers, resources);
		}

		if (auto foundViewData = kernel.FindUniforms("viewData"))
		{
			auto& viewData           = *foundViewData;
			viewData["viewProj"]     = draw.viewState.viewProj;
			viewData["prevViewProj"] = draw.viewState.prevViewProj;
			viewData["jitter"]       = draw.viewState.jitter;
			viewData["prevJitter"]   = draw.viewState.prevJitter;
			viewData["time"]         = draw.clock.time;
			viewData["prevTime"]     = draw.clock.prevTime;
		}

		if (auto foundExpansion = kernel.FindUniforms("expansionData"))
		{
			BindSceneBuffers(*foundExpansion, c_ExpansionBuffers, resources);
			(*foundExpansion)["baseTable"] = idl::BaseTable::kPsoBucketed;
		}

		if (auto foundMatData = kernel.FindUniforms("materialData"))
		{
			auto& matData        = *foundMatData;
			matData["materials"] = draw.materialArena;
			matData["anisoLinearWrapSampler"].SetIfValid(draw.samplers.anisoLinearWrap);
			matData["alphaHashSeed"].SetIfValid(draw.viewState.alphaHashSeed);
		}
	}

	void
	StaticDepthPass::Execute(const DrawData& draw, const PassContext& resources)
	{
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(
			m_HardwareCullKernel.pipeline.IsInitialized() &&
				m_MaterialCullKernel.pipeline.IsInitialized(),
			"Static depth pipelines must be initialized");

		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		auto gfxState = MeshletState();
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		gfxState.frameBuffer  = FrameBuffer().SetDepthAttachment(draw.targets.staticDepth);
		gfxState.indirectArgs = resources.GetBuffer(c_CompactDispatchArgsName);

		// One bucket per dispatch: the cbuffer is re-uploaded on every dispatch, so rewriting
		// psoIndex and cullBackfaces between them is sound (docs/uniforms.md).
		const auto dispatch = [&](MeshletKernel& kernel, const uint16_t pso) {
			if (auto expansion = kernel.FindUniforms("expansionData"))
			{
				(*expansion)["psoIndex"]      = static_cast<uint32_t>(pso);
				(*expansion)["cullBackfaces"] = MeshStageCullsBackfaces(pso);
			}

			gfxState.kernel = &kernel;
			cmd->SetMeshletState(gfxState);
			cmd->DispatchMeshIndirect(pso);
		};

		BindKernel(m_HardwareCullKernel, draw, resources);
		BindKernel(m_MaterialCullKernel, draw, resources);

		for (const uint16_t pso : StaticOpaquePsos())
		{
			const RasterCullMode cull = ForwardPass::PsoCullMode(pso);
			gassert(
				cull == RasterCullMode::kBack || cull == RasterCullMode::kNone,
				"StaticDepthPass mirrors back-face or no hardware culling only");

			dispatch(
				cull == RasterCullMode::kNone ? m_MaterialCullKernel : m_HardwareCullKernel,
				pso);
		}

		const auto buckets = StaticCoverageBuckets();
		for (size_t i = 0; i < buckets.size(); ++i)
		{
			// A bucket never demanded has no kernel -- and, by the same fact, no instances.
			if (!m_CoverageKernels[i].pipeline.IsInitialized())
			{
				continue;
			}

			BindKernel(m_CoverageKernels[i], draw, resources);
			dispatch(m_CoverageKernels[i], buckets[i].pso);
		}
	}
}
