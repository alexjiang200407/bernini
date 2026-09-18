#include "passes/StaticDepthPass.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "passes/BindingNameCheck.h"
#include "passes/DrawData.h"
#include "passes/SceneBindings.h"
#include "passes/draw_bucket_config.h"
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
			"drawBucketIndex"sv,
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
	StaticDepthPass::Init(IDevice* device, PipelineBatch& pipelines, const DrawBucketTable& buckets)
	{
		gassert(device != nullptr, "Device must be initialized");

		m_DrawBuckets = &buckets;

		// Opaque depth does not depend on the material, so the opaque buckets share a depth-only
		// pixel stage and differ only in where back faces are culled.
		pipelines.Add(
			m_HardwareCullKernel,
			DepthPipelineDesc(device, c_PixelSrc, RasterCullMode::kBack));
		pipelines.Add(
			m_MaterialCullKernel,
			DepthPipelineDesc(device, c_PixelSrc, RasterCullMode::kNone));
	}

	void
	StaticDepthPass::AddDrawBucketKernels(
		IDevice*              device,
		PipelineBatch&        pipelines,
		const DrawBucketMask& buckets)
	{
		gassert(device != nullptr, "Device must be initialized");

		const uint32_t count = m_DrawBuckets->Count();
		if (m_CoverageKernels.size() < count)
		{
			m_CoverageKernels.resize(count);
		}

		for (uint32_t bucket = 0; bucket < count; ++bucket)
		{
			if (!buckets.test(bucket) || m_CoverageKernels[bucket].pipeline.IsInitialized())
			{
				continue;
			}

			const DrawBucketDesc& desc = m_DrawBuckets->Desc(bucket);
			if (desc.geom != GeomType::kStaticMesh ||
			    (desc.layer != LayerType::kMask && desc.layer != LayerType::kHashed))
			{
				continue;
			}

			pipelines.Add(
				m_CoverageKernels[bucket],
				DepthPipelineDesc(
					device,
					DrawBucketCoveragePixelSrc(desc),
					DrawBucketCullMode(desc)));
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
		// and EnsureDrawBucketPipelinesExist re-checks after every build.
		if (!AnyInitialized(m_CoverageKernels))
		{
			return;
		}

		BindingNameCheck("StaticDepthPass"sv, m_CoverageKernels)
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
			(*foundExpansion)["baseTable"] = idl::BaseTable::kDrawBucketed;
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
		gfxState.frameBuffer   = FrameBuffer().SetDepthAttachment(draw.targets.staticDepth);
		gfxState.indirectArgs  = resources.GetBuffer(c_CompactDispatchArgsName);
		gfxState.commandCounts = gfxState.indirectArgs;

		// One bucket per dispatch: the cbuffer is re-uploaded on every dispatch, so rewriting
		// drawBucketIndex and cullBackfaces between them is sound (docs/uniforms.md).
		const auto dispatch = [&](MeshletKernel& kernel, const uint32_t bucket) {
			if (auto expansion = kernel.FindUniforms("expansionData"))
			{
				// A bucket the pipeline culls in hardware leaves the mesh stage nothing to do.
				(*expansion)["drawBucketIndex"] = bucket;
				(*expansion)["cullBackfaces"] =
					DrawBucketCullMode(m_DrawBuckets->Desc(bucket)) == RasterCullMode::kNone ? 1u :
																							   0u;
			}

			gfxState.kernel = &kernel;
			cmd->SetMeshletState(gfxState);
			cmd->DispatchMeshIndirectCount(bucket, DrawBucketCountIndex(bucket));
		};

		BindKernel(m_HardwareCullKernel, draw, resources);
		BindKernel(m_MaterialCullKernel, draw, resources);

		// Statics only, to the table's live count: opaque buckets through the two shared
		// depth-only kernels, coverage layers through their own demand-built kernels, blended
		// never -- a blended surface writes no depth for the receiver to reconstruct.
		for (uint32_t bucket = 0, count = m_DrawBuckets->Count(); bucket < count; ++bucket)
		{
			const DrawBucketDesc& desc = m_DrawBuckets->Desc(bucket);
			if (desc.geom != GeomType::kStaticMesh)
			{
				continue;
			}

			if (desc.layer == LayerType::kOpaque)
			{
				dispatch(
					DrawBucketCullMode(desc) == RasterCullMode::kNone ? m_MaterialCullKernel :
																		m_HardwareCullKernel,
					bucket);
				continue;
			}

			// A bucket never demanded has no kernel -- and, by the same fact, no instances.
			if (bucket < m_CoverageKernels.size() &&
			    m_CoverageKernels[bucket].pipeline.IsInitialized())
			{
				BindKernel(m_CoverageKernels[bucket], draw, resources);
				dispatch(m_CoverageKernels[bucket], bucket);
			}
		}
	}
}
