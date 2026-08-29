#include "passes/ForwardPass.h"
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "idl/BaseTable.h"
#include "passes/BinderNames.h"
#include "passes/DrawData.h"
#include "passes/SceneBindings.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/FrameBuffer.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"
#include "scene/Scene.h"
#include "scene/scene_buffer_names.h"
#include "types/RenderState.h"
#include "uniforms/Uniforms.h"
#include "util/util.h"
#include <bgl/ISceneView.h>
#include <bgl/PsoType.h>

namespace bgl
{
	namespace
	{
		struct MaterialBuffer
		{
			std::string_view graphName;
			std::string_view uniformKey;
			BarrierAccess    access;
			BarrierSync      sync;
		};

		static constexpr std::array<MaterialBuffer, 2> c_MaterialBuffers = { {
			{
				c_PbrMaterialBufferName,
				"pbrMaterials",
				BarrierAccessFlag::kShaderResource,
				BarrierSyncFlag::kVertexShader,
			},
			{
				c_LooseMaterialBufferName,
				"looseMaterials",
				BarrierAccessFlag::kShaderResource,
				BarrierSyncFlag::kVertexShader,
			},
		} };

		// Every member BindKernel and its callers name, beyond the buffer tables above. Kept beside
		// the code that writes them so BinderNames catches a shader rename at startup: a
		// stale name is indistinguishable from an absent one once binding reaches IsValid().
		constexpr std::array<std::string_view, 6> c_ViewDataFields = {
			"viewProj"sv, "prevViewProj"sv, "jitter"sv, "prevJitter"sv, "time"sv, "prevTime"sv,
		};

		constexpr std::array<std::string_view, 9> c_MaterialDataFields = {
			"anisoLinearWrapSampler"sv,
			"linearClampSampler"sv,
			"irradianceMap"sv,
			"prefilterMap"sv,
			"brdfLUT"sv,
			"cameraPos"sv,
			"exposure"sv,
			"envRotation"sv,
			"alphaHashSeed"sv,
		};

		constexpr std::array<std::string_view, 3> c_ExpansionDataFields = {
			"psoIndex"sv,
			"baseTable"sv,
			"compactedInstances"sv,
		};

		constexpr auto c_MotionVectorFormat = Format::RG16_FLOAT;
		constexpr auto c_SceneColorFormat   = Format::RGBA16_FLOAT;

		constexpr auto c_GeomSrc             = "Forward_StaticMesh"sv;
		constexpr auto c_VatGeomSrc          = "Forward_VatMesh"sv;
		constexpr auto c_SkinnedGeomSrc      = "Forward_SkinnedMesh"sv;
		constexpr auto c_AnyGeomSrc          = "Forward_AnyMesh"sv;
		constexpr auto c_PbrPixelSrc         = "Forward_PBR"sv;
		constexpr auto c_LoosePixelSrc       = "Forward_PBR_Loose"sv;
		constexpr auto c_NullPixelSrc        = "Forward_Null"sv;
		constexpr auto c_PbrCutoutPixelSrc   = "Forward_PBR_AlphaTest"sv;
		constexpr auto c_LooseCutoutPixelSrc = "Forward_PBR_Loose_AlphaTest"sv;
		constexpr auto c_PbrHashedPixelSrc   = "Forward_PBR_HashedAlpha"sv;
		constexpr auto c_LooseHashedPixelSrc = "Forward_PBR_Loose_HashedAlpha"sv;
		constexpr auto c_TransparentSrc      = "Forward_Transparent"sv;
		constexpr auto c_AssertPixelSrc      = "Forward_Assert"sv;

		struct PsoConfig
		{
			std::string_view pixelSrc;
			RasterCullMode   cull;
			bool             depthWrite;
			bool             blend;
			ComparisonFunc   depthFunc = ComparisonFunc::kLess;
			std::string_view geomSrc   = c_GeomSrc;
		};

		// Order MUST match PsoType (bgl/PsoType.h, generated from idl/src/PsoType.slang).
		static constexpr std::array<PsoConfig, c_PsoCount> c_Psos = { {
			// kOpaque_StaticMesh_Null
			{ c_NullPixelSrc, RasterCullMode::kBack, true, false },
			// kOpaque_StaticMesh_PBR
			{ c_PbrPixelSrc, RasterCullMode::kBack, true, false },
			// kOpaque_StaticMesh_LoosePbr
			{ c_LoosePixelSrc, RasterCullMode::kBack, true, false },
			// kAlphaTest_StaticMesh_PBR
			{ c_PbrCutoutPixelSrc, RasterCullMode::kNone, true, false },
			// kAlphaTest_StaticMesh_LoosePbr
			{ c_LooseCutoutPixelSrc, RasterCullMode::kNone, true, false },
			// kTransparent_StaticMesh_PBR: the whole sorted list draws through this one pipeline,
			// so its geometry stage is the tier-branching one.
			{ c_TransparentSrc,
			  RasterCullMode::kNone,
			  false,
			  true,
			  ComparisonFunc::kLess,
			  c_AnyGeomSrc },
			// kTransparent_StaticMesh_LoosePbr
			{ c_TransparentSrc,
			  RasterCullMode::kNone,
			  false,
			  true,
			  ComparisonFunc::kLess,
			  c_AnyGeomSrc },
			// kHashedAlpha_StaticMesh_PBR: opaque shape -- the coverage is stochastic, the depth is not.
			{ c_PbrHashedPixelSrc, RasterCullMode::kNone, true, false },
			// kHashedAlpha_StaticMesh_LoosePbr
			{ c_LooseHashedPixelSrc, RasterCullMode::kNone, true, false },
			// kAssert_StaticMesh
			{ c_AssertPixelSrc, RasterCullMode::kBack, true, false },
			// kOpaque_VatMesh_PBR
			{ c_PbrPixelSrc,
			  RasterCullMode::kBack,
			  true,
			  false,
			  ComparisonFunc::kLess,
			  c_VatGeomSrc },
			// kAlphaTest_VatMesh_PBR: an opaque draw that discards, so it needs no sorting.
			{ c_PbrCutoutPixelSrc,
			  RasterCullMode::kNone,
			  true,
			  false,
			  ComparisonFunc::kLess,
			  c_VatGeomSrc },
			// kHashedAlpha_VatMesh_PBR: stochastic coverage, so also an opaque shape.
			{ c_PbrHashedPixelSrc,
			  RasterCullMode::kNone,
			  true,
			  false,
			  ComparisonFunc::kLess,
			  c_VatGeomSrc },
			// kTransparent_VatMesh_PBR: never drawn from its own bucket -- the depth-sorted list is,
			// through kTransparent_StaticMesh_PBR's kernel. The row exists so the bucket has one.
			{ c_TransparentSrc,
			  RasterCullMode::kNone,
			  false,
			  true,
			  ComparisonFunc::kLess,
			  c_AnyGeomSrc },
			// kOpaque_SkinnedMesh_PBR
			{ c_PbrPixelSrc,
			  RasterCullMode::kBack,
			  true,
			  false,
			  ComparisonFunc::kLess,
			  c_SkinnedGeomSrc },
			// kAlphaTest_SkinnedMesh_PBR: an opaque draw that discards, so it needs no sorting.
			{ c_PbrCutoutPixelSrc,
			  RasterCullMode::kNone,
			  true,
			  false,
			  ComparisonFunc::kLess,
			  c_SkinnedGeomSrc },
			// kHashedAlpha_SkinnedMesh_PBR: stochastic coverage, so also an opaque shape.
			{ c_PbrHashedPixelSrc,
			  RasterCullMode::kNone,
			  true,
			  false,
			  ComparisonFunc::kLess,
			  c_SkinnedGeomSrc },
			// kTransparent_SkinnedMesh_PBR: as above, a bucket rather than a draw.
			{ c_TransparentSrc,
			  RasterCullMode::kNone,
			  false,
			  true,
			  ComparisonFunc::kLess,
			  c_AnyGeomSrc },
		} };

		static_assert(
			std::ranges::none_of(c_Psos, [](const PsoConfig& cfg) { return cfg.pixelSrc.empty(); }),
			"every PsoType needs a row in c_Psos; a missing one silently value-initializes to an "
			"empty pixel shader");

		// The PsoType each row above is, for the startup report. Order MUST match PsoType, like
		// c_Psos -- these are the names a client shows while the pipeline is being compiled.
		static constexpr std::array<std::string_view, c_PsoCount> c_PsoNames = { {
			"kOpaque_StaticMesh_Null"sv,
			"kOpaque_StaticMesh_PBR"sv,
			"kOpaque_StaticMesh_LoosePbr"sv,
			"kAlphaTest_StaticMesh_PBR"sv,
			"kAlphaTest_StaticMesh_LoosePbr"sv,
			"kTransparent_StaticMesh_PBR"sv,
			"kTransparent_StaticMesh_LoosePbr"sv,
			"kHashedAlpha_StaticMesh_PBR"sv,
			"kHashedAlpha_StaticMesh_LoosePbr"sv,
			"kAssert_StaticMesh"sv,
			"kOpaque_VatMesh_PBR"sv,
			"kAlphaTest_VatMesh_PBR"sv,
			"kHashedAlpha_VatMesh_PBR"sv,
			"kTransparent_VatMesh_PBR"sv,
			"kOpaque_SkinnedMesh_PBR"sv,
			"kAlphaTest_SkinnedMesh_PBR"sv,
			"kHashedAlpha_SkinnedMesh_PBR"sv,
			"kTransparent_SkinnedMesh_PBR"sv,
		} };

		static_assert(
			std::ranges::none_of(c_PsoNames, &std::string_view::empty),
			"every PsoType needs a name; a missing row value-initializes to an empty one");

		MeshletKernel
		BuildForwardKernel(IDevice* device, const PsoConfig& cfg)
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

			// Premultiplied: Forward_Transparent returns radiance already weighted by its own
			// coverage, so the reflection reaches the film undimmed by the material's alpha while
			// the transmitted lobe is thinned in the shader. kSrcAlpha here would scale both.
			auto blend = BlendState{};
			if (cfg.blend)
			{
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
			}

			pipelineDesc.renderState =
				RenderState().SetRasterState(raster).SetBlendState(blend).SetDepthStencilState(
					depth);

			return device->CreateMeshletKernel(pipelineDesc);
		}
	}

	void
	ForwardPass::Init(IDevice* device, PipelineBuild& build)
	{
		gassert(device != nullptr, "Device must be initialized");

		for (uint16_t pso = 0; pso < c_PsoCount; ++pso)
		{
			build.Step(c_PsoNames[pso]);
			m_Kernels[pso] = BuildForwardKernel(device, c_Psos[pso]);
		}

		BinderNames("ForwardPass"sv, m_Kernels)
			.Check("forwardData"sv, GetUniformKeys(c_ForwardDataBuffers))
			.Check("expansionData"sv, GetUniformKeys(c_ExpansionBuffers))
			.Check("expansionData"sv, c_ExpansionDataFields)
			.Check("viewData"sv, c_ViewDataFields)
			.Check("materialData"sv, GetUniformKeys(c_MaterialBuffers))
			.Check("materialData"sv, c_MaterialDataFields)
			.Check("vatData"sv, GetUniformKeys(c_VatBuffers))
			.Check("skinnedData"sv, GetUniformKeys(c_SkinnedBuffers));
	}

	void
	ForwardPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		auto desc = PassDesc();

		desc.SetName("Forward {}", draw.drawIdx)
			.AddTextureArg(
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
		                   BarrierAccessFlag::kIndirectArgument })
			.AddBufferArg(
				BufferArg{ std::string(c_SortedTransparentInstancesName),
		                   BarrierSyncFlag::kVertexShader,
		                   BarrierAccessFlag::kUnorderedAccess })
			.AddBufferArg(
				BufferArg{ std::string(c_TransparentDispatchArgsName),
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

		for (const auto& binding : c_MaterialBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		for (const auto& binding : c_VatBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		for (const auto& binding : c_SkinnedBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		desc.SetExec([this, draw](const PassContext& resources) { Execute(draw, resources); });

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
		}

		if (auto foundVatData = kernel.FindUniforms("vatData"))
		{
			BindSceneBuffers(*foundVatData, c_VatBuffers, resources);
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
			for (const auto& binding : c_MaterialBuffers)
			{
				matData[binding.uniformKey].SetIfValid(resources.GetBuffer(binding.graphName));
			}

			matData["anisoLinearWrapSampler"].SetIfValid(draw.samplers.anisoLinearWrap);
			matData["linearClampSampler"].SetIfValid(draw.samplers.linearClamp);

			// IBL maps: assigning the RHI TextureHandle writes a descriptor handle into the
			// shader-side handle's sole member.
			matData["irradianceMap"].SetIfValid(draw.lighting.env.irradiance);
			matData["prefilterMap"].SetIfValid(draw.lighting.env.prefilter);
			matData["brdfLUT"].SetIfValid(draw.lighting.env.brdfLut);
			matData["cameraPos"].SetIfValid(draw.viewState.cameraPos);
			matData["exposure"].SetIfValid(draw.lighting.exposure);
			matData["envRotation"].SetIfValid(draw.lighting.envRotation);
			matData["alphaHashSeed"].SetIfValid(draw.viewState.alphaHashSeed);
		}
	}

	void
	ForwardPass::Execute(const DrawData& draw, const PassContext& resources)
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

		// Opaque and alpha-test: PSO-bucketed, drawn indirect over the counting-sort output. The
		// transparent buckets are skipped here -- their order is depth, not PSO, so they draw below.
		for (uint16_t pso = 0; pso < c_PsoCount; ++pso)
		{
			if (IsTransparentPso(pso))
			{
				continue;
			}

			MeshletKernel& kernel = m_Kernels[pso];
			gassert(kernel.pipeline.IsInitialized(), "Pass pipeline must be initialized");

			BindKernel(kernel, draw, resources);
			if (auto expansionData = kernel.FindUniforms("expansionData"))
			{
				(*expansionData)["psoIndex"]  = static_cast<uint32_t>(pso);
				(*expansionData)["baseTable"] = idl::BaseTable::kPsoBucketed;
			}

			gfxState.kernel       = &kernel;
			gfxState.indirectArgs = dispatchArgs;
			cmd->SetMeshletState(gfxState);
			cmd->DispatchMeshIndirect(pso);
		}

		DrawTransparent(draw, resources);
	}

	void
	ForwardPass::DrawTransparent(const DrawData& draw, const PassContext& resources)
	{
		ICommandList* cmd             = resources.GetCommandList();
		const auto    sortedInstances = resources.GetBuffer(c_SortedTransparentInstancesName);
		const auto    transparentArgs = resources.GetBuffer(c_TransparentDispatchArgsName);

		// The sort leaves the whole list farthest-first and both transparent PSOs share one pipeline,
		// so the depth-sorted draw is a single dispatch whose count lives entirely on the GPU.
		//
		// Colour only: a blend PSO declares one rtvFormat, so the velocity buffer must not be attached
		// here -- a blended surface has no single depth to reproject.
		auto colorState = MeshletState();
		colorState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		colorState.frameBuffer = FrameBuffer()
		                             .AddColorAttachment(draw.targets.sceneColor)
		                             .SetDepthAttachment(draw.targets.depth);

		MeshletKernel& kernel =
			m_Kernels[static_cast<size_t>(PsoType::kTransparent_StaticMesh_PBR)];
		gassert(kernel.pipeline.IsInitialized(), "Pass pipeline must be initialized");

		BindKernel(kernel, draw, resources);
		if (auto expansionData = kernel.FindUniforms("expansionData"))
		{
			(*expansionData)["compactedInstances"] = sortedInstances;
			(*expansionData)["baseTable"]          = idl::BaseTable::kDepthSorted;
		}

		colorState.kernel       = &kernel;
		colorState.indirectArgs = transparentArgs;
		cmd->SetMeshletState(colorState);

		// The argument index within `transparentArgs`, which holds a single grid now that the sorted
		// list is drawn whole. The opaque path indexes the same way, by PsoType.
		cmd->DispatchMeshIndirect(0);
	}

}
