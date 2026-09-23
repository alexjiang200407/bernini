#include "passes/ForwardPass.h"
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
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
#include "resource/ResourceManager.h"
#include "resource/Shader.h"
#include "scene/Scene.h"
#include "scene/scene_buffer_names.h"
#include "types/Barrier.h"
#include "types/BlendState.h"
#include "types/DepthStencilState.h"
#include "types/Format.h"
#include "types/RasterState.h"
#include "types/RenderState.h"
#include "uniforms/Uniforms.h"
#include <array>
#include <bgl/GeomType.h>
#include <bgl/ISceneView.h>
#include <bgl_common/gassert.h>
#include <bgl_common/idl/BaseTable.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		// Every member BindKernel and its callers name, beyond the buffer tables above. Kept beside
		// the code that writes them so BindingNameCheck catches a shader rename at startup: a
		// stale name is indistinguishable from an absent one once binding reaches IsValid().
		constexpr std::array<std::string_view, 6> c_ViewDataFields = {
			"viewProj"sv, "prevViewProj"sv, "jitter"sv, "prevJitter"sv, "time"sv, "prevTime"sv,
		};

		// clang-format off
		constexpr std::array<std::string_view, 11> c_MaterialDataFields = {
			"anisoLinearWrapSampler"sv,
			"linearClampSampler"sv,
			"irradianceMap"sv,
			"prefilterMap"sv,
			"brdfLUT"sv,
			"cameraPos"sv,
			"exposure"sv,
			"envRotation"sv,
			"alphaHashSeed"sv,
			"sunDirection"sv,
			"sunRadiance"sv,
		};
		// clang-format on

		constexpr std::array<std::string_view, 4> c_ExpansionDataFields = {
			"drawBucketIndex"sv,
			"baseTable"sv,
			"compactedInstances"sv,
			"cullBackfaces"sv,
		};

		constexpr auto c_SceneColorFormat = Format::RGBA16_FLOAT;

		// The shared blend kernel's programs: the whole depth-sorted list draws through this one
		// pipeline, and AnyMesh branches tier per instance, so no bucket needs a blend kernel of
		// its own.
		constexpr auto c_AnyGeomSrc     = "programs.forward.AnyMesh"sv;
		constexpr auto c_TransparentSrc = "programs.forward.Transparent"sv;

		struct PsoConfig
		{
			std::string      pixelSrc;
			RasterCullMode   cull;
			bool             depthWrite;
			bool             blend;
			ComparisonFunc   depthFunc = ComparisonFunc::kLess;
			std::string_view geomSrc;
		};

		// Every bucket kernel is opaque-shaped; only the shared blend kernel differs.
		PsoConfig
		ConfigFor(const DrawBucketDesc& desc)
		{
			return PsoConfig{ DrawBucketPixelSrc(desc), DrawBucketCullMode(desc),   true, false,
				              ComparisonFunc::kLess,    DrawBucketGeometrySrc(desc) };
		}

		MeshletPipelineDesc
		ForwardPipelineDesc(IDevice* device, const PsoConfig& cfg)
		{
			auto pipelineDesc = MeshletPipelineDesc();

			pipelineDesc.ampShader  = device->CreateShader(std::string(cfg.geomSrc), "ASMain");
			pipelineDesc.meshShader = device->CreateShader(std::string(cfg.geomSrc), "MSMain");

			pipelineDesc.pixelShader = device->CreateShader(std::string(cfg.pixelSrc), "PSMain");

			pipelineDesc.AddRtvFormat(c_SceneColorFormat);

			// The rtvFormats count is what the bound framebuffer must match, so a blend PSO omitting
			// this is also what keeps the velocity buffer out of its attachments.
			if (!cfg.blend)
			{
				pipelineDesc.AddRtvFormat(c_MotionVectorFormat);
			}
			pipelineDesc.SetDsvFormat(Format::D24S8);

			auto raster = RasterState();
			raster.SetFillMode(RasterFillMode::kSolid)
				.SetCullMode(cfg.cull)
				.SetFrontCounterClockwise(true)
				.SetDepthClipEnable(true);

			auto depth = DepthStencilState{};
			depth.SetDepthTestEnable(true)
				.SetDepthWriteEnable(cfg.depthWrite)
				.SetDepthFunc(cfg.depthFunc)
				.SetStencilEnable(false);

			// Premultiplied: programs.forward.Transparent returns radiance already weighted by its own
			// coverage, so the reflection reaches the film undimmed by the material's alpha while
			// the transmitted lobe is thinned in the shader. kSrcAlpha here would scale both.
			auto blend = BlendState{};
			// Composited colour has no single depth; zero alpha excludes it from TAA depth validation.
			if (cfg.blend)
			{
				blend.SetRenderTarget(
					0,
					BlendState::RenderTarget{}
						.EnableBlend()
						.SetSrcBlend(BlendFactor::kOne)
						.SetDestBlend(BlendFactor::kInvSrcAlpha)
						.SetBlendOp(BlendOp::kAdd)
						.SetSrcBlendAlpha(BlendFactor::kZero)
						.SetDestBlendAlpha(BlendFactor::kZero)
						.SetBlendOpAlpha(BlendOp::kAdd));
			}

			pipelineDesc.renderState =
				RenderState().SetRasterState(raster).SetBlendState(blend).SetDepthStencilState(
					depth);

			return pipelineDesc;
		}
	}

	void
	ForwardPass::Init(const PassInitContext& ctx)
	{
		gassert(ctx.device != nullptr, "Device must be initialized");

		gassert(ctx.drawBucketTable != nullptr, "The pass keys its kernels by draw bucket");
		m_DrawBucketTable = ctx.drawBucketTable;
	}

	void
	ForwardPass::AddDrawBucketKernels(const PassInitContext& ctx, const DrawBucketMask& demanded)
	{
		gassert(ctx.device != nullptr, "Device must be initialized");

		const uint32_t count = m_DrawBucketTable->Count();
		if (m_Kernels.size() < count)
		{
			m_Kernels.resize(count);
		}

		for (uint32_t bucket = 0; bucket < count; ++bucket)
		{
			if (demanded.test(bucket) && !m_Kernels[bucket].pipeline.IsInitialized())
			{
				gassert(
					!m_DrawBucketTable->Transparent(bucket),
					"A transparent bucket demands the shared kernel, never one of its own");
				ctx.pipelines->Add(
					m_Kernels[bucket],
					ForwardPipelineDesc(ctx.device, ConfigFor(m_DrawBucketTable->Desc(bucket))));
			}
		}
	}

	void
	ForwardPass::AddTransparentKernel(const PassInitContext& ctx)
	{
		gassert(ctx.device != nullptr, "Device must be initialized");

		if (!m_TransparentKernel.pipeline.IsInitialized())
		{
			ctx.pipelines->Add(
				m_TransparentKernel,
				ForwardPipelineDesc(
					ctx.device,
					PsoConfig{ std::string(c_TransparentSrc),
			                   RasterCullMode::kNone,
			                   false,
			                   true,
			                   ComparisonFunc::kLess,
			                   c_AnyGeomSrc }));
		}
	}

	void
	ForwardPass::CheckBindings() const
	{
		CheckKernelNames(m_Kernels);
		CheckKernelNames({ &m_TransparentKernel, 1 });
	}

	void
	ForwardPass::CheckKernelNames(std::span<const MeshletKernel> kernels) const
	{
		// The buckets are demand-built, so nothing reads their names off until a first one is;
		// EnsureDrawBucketPipelinesExist re-checks after every build.
		if (!AnyInitialized(kernels))
		{
			return;
		}

		BindingNameCheck("ForwardPass"sv, kernels)
			.Check("forwardData"sv, GetUniformKeys(c_ForwardDataBuffers))
			.Check("expansionData"sv, GetUniformKeys(c_ExpansionBuffers))
			.Check("expansionData"sv, c_ExpansionDataFields)
			.Check("viewData"sv, c_ViewDataFields)
			.Check("materialData"sv, GetUniformKeys(c_MaterialBuffers))
			.Check("materialData"sv, c_MaterialDataFields)
			.Check("skinnedData"sv, GetUniformKeys(c_SkinnedBuffers));
	}

	void
	ForwardPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw, const ForwardPhase phase)
	{
		auto desc = PassDesc();

		if (phase == ForwardPhase::kStatic)
		{
			desc.SetName("Forward Static {}", draw.drawIdx);
		}
		else
		{
			desc.SetName("Forward Units {}", draw.drawIdx);
		}

		desc.AddTextureArg(
				TextureArg{ std::string(c_BackbufferName),
		                    BarrierSyncFlag::kRenderTarget,
		                    BarrierAccessFlag::kRenderTarget,
		                    BarrierLayout::kRenderTarget })
			.AddTextureArg(
				TextureArg{ std::string(c_MotionVectorsName),
		                    BarrierSyncFlag::kRenderTarget,
		                    BarrierAccessFlag::kRenderTarget,
		                    BarrierLayout::kRenderTarget })
			.AddTextureArg(
				TextureArg{ std::string(c_DepthName),
		                    BarrierSyncFlag::kDepthStencil,
		                    BarrierAccessFlag::kDepthWrite,
		                    BarrierLayout::kDepthWrite })
			.AddBufferArg(
				BufferArg{ std::string(c_CompactDispatchArgsName),
		                   BarrierSyncFlag::kIndirectArgument,
		                   BarrierAccessFlag::kIndirectArgument });

		if (phase == ForwardPhase::kUnits)
		{
			desc.AddBufferArg(
					BufferArg{ std::string(c_SortedTransparentInstancesName),
			                   BarrierSyncFlag::kVertexShader,
			                   BarrierAccessFlag::kUnorderedAccess })
				.AddBufferArg(
					BufferArg{ std::string(c_TransparentDispatchArgsName),
			                   BarrierSyncFlag::kIndirectArgument,
			                   BarrierAccessFlag::kIndirectArgument });

			for (const auto& binding : c_SkinnedBuffers)
			{
				desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
			}
		}

		for (const auto& binding : c_ForwardDataBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		for (const auto& binding : c_ExpansionBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		DeclareMeshletCullBuffers(desc);

		for (const auto& binding : c_MaterialBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		desc.SetExec(
			[this, draw, phase](const PassContext& resources) { Execute(draw, resources, phase); });

		fg.AddPass(std::move(desc));
	}

	void
	ForwardPass::BindKernel(
		MeshletKernel&     kernel,
		const DrawData&    draw,
		const PassContext& resources)
	{
		if (auto foundForwardData = kernel.FindUniforms("forwardData"))
		{
			BindSceneBuffers(*foundForwardData, c_ForwardDataBuffers, resources);
		}

		if (auto foundExpansion = kernel.FindUniforms("expansionData"))
		{
			BindSceneBuffers(*foundExpansion, c_ExpansionBuffers, resources);
			BindMeshletCullBuffers(*foundExpansion, resources);
		}

		if (auto foundSkinnedData = kernel.FindUniforms("skinnedData"))
		{
			BindSceneBuffers(*foundSkinnedData, c_SkinnedBuffers, resources);
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

		if (auto foundMatData = kernel.FindUniforms("materialData"))
		{
			auto& matData = *foundMatData;
			// Bound from the draw rather than the graph: the pair is one allocation and two
			// descriptors, and the graph tracks resource state -- a view is not a resource. It is
			// still declared to the graph (c_MaterialBuffers) so the arena's barriers are placed.
			matData["materials"] = draw.materialArena;

			matData["anisoLinearWrapSampler"].SetIfValid(draw.samplers.anisoLinearWrap);
			matData["linearClampSampler"].SetIfValid(draw.samplers.linearClamp);

			matData["irradianceMap"].SetIfValid(draw.lighting.env.irradiance);
			matData["prefilterMap"].SetIfValid(draw.lighting.env.prefilter);
			matData["brdfLUT"].SetIfValid(draw.lighting.env.brdfLut);
			matData["cameraPos"].SetIfValid(draw.viewState.cameraPos);
			matData["exposure"].SetIfValid(draw.lighting.exposure);
			matData["envRotation"].SetIfValid(draw.lighting.envRotation);
			matData["alphaHashSeed"].SetIfValid(draw.viewState.alphaHashSeed);
			matData["sunDirection"].SetIfValid(draw.lighting.sunDirection);
			matData["sunRadiance"].SetIfValid(draw.lighting.sunRadiance);
		}
	}

	void
	ForwardPass::Execute(
		const DrawData&    draw,
		const PassContext& resources,
		const ForwardPhase phase)
	{
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");

		if (draw.view->GetInstanceCount() == 0)
		{
			return;
		}

		// Colour + velocity, matching the two rtvFormats every non-blend PSO declares.
		auto gfxState = MeshletState();
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		gfxState.frameBuffer = FrameBuffer()
		                           .AddColorAttachment(draw.targets.sceneColor)
		                           .AddColorAttachment(draw.targets.motionVector)
		                           .SetDepthAttachment(draw.targets.depth);

		const auto dispatchArgs = resources.GetBuffer(c_CompactDispatchArgsName);

		// Opaque and alpha-test: bucketed, drawn indirect over the counting-sort output, to the
		// table's live count. The transparent buckets are skipped here -- their order is depth,
		// not bucket, so they draw below.
		const bool staticPhase = phase == ForwardPhase::kStatic;
		for (uint32_t bucket = 0, count = m_DrawBucketTable->Count(); bucket < count; ++bucket)
		{
			const bool staticBucket = m_DrawBucketTable->Desc(bucket).geom == GeomType::kStaticMesh;
			if (m_DrawBucketTable->Transparent(bucket) || staticBucket != staticPhase)
			{
				continue;
			}

			// A bucket never demanded has no kernel -- and, by the same fact, no instances to draw.
			if (!DrawBucketInitialized(bucket))
			{
				continue;
			}

			MeshletKernel& kernel = m_Kernels[bucket];
			BindKernel(kernel, draw, resources);
			if (auto expansionData = kernel.FindUniforms("expansionData"))
			{
				(*expansionData)["drawBucketIndex"] = bucket;
				(*expansionData)["baseTable"]       = idl::BaseTable::kDrawBucketed;
				(*expansionData)["cullBackfaces"] =
					DrawBucketMeshStageCullsBackfaces(m_DrawBucketTable->Desc(bucket));
			}

			gfxState.kernel        = &kernel;
			gfxState.indirectArgs  = dispatchArgs;
			gfxState.commandCounts = dispatchArgs;
			cmd->SetMeshletState(gfxState);
			cmd->DispatchMeshIndirectCount(bucket, DrawBucketCountIndex(bucket));
		}

		if (!staticPhase)
		{
			DrawTransparent(draw, resources);
		}
	}

	void
	ForwardPass::DrawTransparent(const DrawData& draw, const PassContext& resources)
	{
		ICommandList* cmd             = resources.GetCommandList();
		const auto    sortedInstances = resources.GetBuffer(c_SortedTransparentInstancesName);
		const auto    transparentArgs = resources.GetBuffer(c_TransparentDispatchArgsName);

		// The sort leaves the whole list farthest-first and every transparent bucket shares one kernel,
		// so the depth-sorted draw is a single dispatch whose count lives entirely on the GPU.
		//
		// Colour only: a blend PSO declares one rtvFormat, so the velocity buffer must not be attached
		// here -- a blended surface has no single depth to reproject.
		auto colorState = MeshletState();
		colorState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		colorState.frameBuffer = FrameBuffer()
		                             .AddColorAttachment(draw.targets.sceneColor)
		                             .SetDepthAttachment(draw.targets.depth);

		// Built whenever any transparent bucket is demanded; absent, the sorted list is empty too.
		MeshletKernel& kernel = m_TransparentKernel;
		if (!kernel.pipeline.IsInitialized())
		{
			return;
		}

		BindKernel(kernel, draw, resources);
		if (auto expansionData = kernel.FindUniforms("expansionData"))
		{
			(*expansionData)["compactedInstances"] = sortedInstances;
			(*expansionData)["baseTable"]          = idl::BaseTable::kDepthSorted;
			(*expansionData)["cullBackfaces"]      = 1u;
		}

		colorState.kernel       = &kernel;
		colorState.indirectArgs = transparentArgs;
		cmd->SetMeshletState(colorState);

		// The argument index within `transparentArgs`, which holds the single grid the whole sorted
		// list draws with; the bucketed path indexes its own buffer by bucket id.
		cmd->DispatchMeshIndirect(0);
	}

}
