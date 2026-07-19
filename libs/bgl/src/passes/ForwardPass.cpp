#include "passes/ForwardPass.h"
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "passes/DrawData.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/FrameBuffer.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"
#include "scene/Scene.h"
#include "types/RenderState.h"
#include "uniforms/Uniforms.h"
#include "util/util.h"
#include <bgl/ISceneView.h>
#include <bgl/PsoType.h>

namespace bgl
{
	namespace
	{
		struct SceneBuffer
		{
			std::string_view graphName;
			std::string_view uniformKey;
			BarrierAccess    access;
			BarrierSync      sync;
		};

		struct MaterialBuffer
		{
			std::string_view graphName;
			std::string_view uniformKey;
			BarrierAccess    access;
			BarrierSync      sync;
		};

		static constexpr std::array<MaterialBuffer, 2> c_MaterialBuffers = { {
			{
				"scene.pbrMaterialBuffer",
				"pbrMaterials",
				BarrierAccessFlag::kShaderResource,
				BarrierSyncFlag::kVertexShader,
			},
			{
				"scene.looseMaterialBuffer",
				"looseMaterials",
				BarrierAccessFlag::kShaderResource,
				BarrierSyncFlag::kVertexShader,
			},
		} };

		static constexpr std::array<SceneBuffer, 10> c_ForwardDataBuffers = {
			{ { "scene.instanceBuffer",
			    "instanceBuffer",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader },
			  { "scene.meshInstanceBuffer",
			    "meshBuffer",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader },
			  { "scene.submeshBuffer",
			    "submeshBuffer",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader },
			  { "scene.meshletBuffer",
			    "meshletBuffer",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader },
			  { "scene.vertexMapBuffer",
			    "vertexMapBuffer",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader },
			  { "scene.vertexDataBuffer",
			    "vertexDataBuffer",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader },
			  { "scene.indexBuffer",
			    "indexBuffer",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader },
			  { "scene.compactedInstances",
			    "compactedInstances",
			    BarrierAccessFlag::kUnorderedAccess,
			    BarrierSyncFlag::kVertexShader },
			  { "compactedInstances.psoPrefixSumBuffer",
			    "psoPrefixSum",
			    BarrierAccessFlag::kUnorderedAccess,
			    BarrierSyncFlag::kVertexShader },
			  { "transparentSort.partitionBase",
			    "transparentPartitionBase",
			    BarrierAccessFlag::kUnorderedAccess,
			    BarrierSyncFlag::kVertexShader } }
		};

		constexpr auto c_DispatchArgsBuffer = "compactedInstances.compactDispatchArgs"sv;

		constexpr auto c_SortedTransparentBuffer = "scene.sortedTransparentInstances"sv;
		constexpr auto c_TransparentArgsBuffer   = "transparentSort.partitionDispatchArgs"sv;

		constexpr auto c_GeomSrc               = "Forward_StaticMesh"sv;
		constexpr auto c_PbrPixelSrc           = "Forward_PBR"sv;
		constexpr auto c_LoosePixelSrc         = "Forward_PBR_Loose"sv;
		constexpr auto c_NullPixelSrc          = "Forward_Null"sv;
		constexpr auto c_PbrCutoutPixelSrc     = "Forward_PBR_AlphaTest"sv;
		constexpr auto c_LooseCutoutPixelSrc   = "Forward_PBR_Loose_AlphaTest"sv;
		constexpr auto c_TransparentSrc        = "Forward_Transparent"sv;
		constexpr auto c_TransparentPrepassSrc = "Forward_Transparent_Prepass"sv;
		constexpr auto c_AssertPixelSrc        = "Forward_Assert"sv;

		struct PsoConfig
		{
			std::string_view pixelSrc;
			RasterCullMode   cull;
			bool             depthWrite;
			bool             blend;
			ComparisonFunc   depthFunc = ComparisonFunc::kLess;

			// A depth-only pass binds no render target and writes no colour (the pre-pass).
			bool depthOnly = false;
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
			// kTransparent_StaticMesh_PBR
			{ c_TransparentSrc, RasterCullMode::kNone, false, true },
			// kTransparent_StaticMesh_LoosePbr
			{ c_TransparentSrc, RasterCullMode::kNone, false, true },
			// kTransparentOcclude_StaticMesh_PBR: front layer only, matched to the pre-pass depth.
			{ c_TransparentSrc, RasterCullMode::kNone, false, true, ComparisonFunc::kEqual },
			// kTransparentOcclude_StaticMesh_LoosePbr
			{ c_TransparentSrc, RasterCullMode::kNone, false, true, ComparisonFunc::kEqual },
			// kAssert_StaticMesh
			{ c_AssertPixelSrc, RasterCullMode::kBack, true, false },
		} };

		// Depth-only pre-pass configs, keyed by the self-occluding PSO they precede. Write depth for
		// the front layer (alpha-discarding the rest), so its Equal-tested colour draw blends once.
		struct PrepassEntry
		{
			PsoType   pso;
			PsoConfig config;
		};

		static constexpr std::array<PrepassEntry, 2> c_Prepasses = { {
			{ PsoType::kTransparentOcclude_StaticMesh_PBR,
			  { c_TransparentPrepassSrc,
			    RasterCullMode::kNone,
			    true,
			    false,
			    ComparisonFunc::kLess,
			    true } },
			{ PsoType::kTransparentOcclude_StaticMesh_LoosePbr,
			  { c_TransparentPrepassSrc,
			    RasterCullMode::kNone,
			    true,
			    false,
			    ComparisonFunc::kLess,
			    true } },
		} };

		static_assert(
			std::ranges::none_of(c_Psos, [](const PsoConfig& cfg) { return cfg.pixelSrc.empty(); }),
			"every PsoType needs a row in c_Psos; a missing one silently value-initializes to an "
			"empty pixel shader");

		MeshletKernel
		BuildForwardKernel(IDevice* device, const PsoConfig& cfg)
		{
			auto pipelineDesc = MeshletPipelineDesc();

			pipelineDesc.ampShader  = device->CreateShader(std::string(c_GeomSrc), "ASMain");
			pipelineDesc.meshShader = device->CreateShader(std::string(c_GeomSrc), "MSMain");

			pipelineDesc.pixelShader = device->CreateShader(std::string(cfg.pixelSrc), "PSMain");

			// A depth-only pre-pass binds no render target: it exists only to write depth.
			if (!cfg.depthOnly)
			{
				pipelineDesc.AddRtvFormat(Format::SBGRA8_UNORM);
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

			auto blend = BlendState{};
			if (cfg.blend)
			{
				blend.SetRenderTarget(
					0,
					BlendState::RenderTarget{}
						.EnableBlend()
						.SetSrcBlend(BlendFactor::kSrcAlpha)
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
	ForwardPass::Init(IDevice* device)
	{
		gassert(device != nullptr, "Device must be initialized");

		for (uint16_t pso = 0; pso < c_PsoCount; ++pso)
		{
			m_Kernels[pso] = BuildForwardKernel(device, c_Psos[pso]);
		}

		for (const PrepassEntry& entry : c_Prepasses)
		{
			m_PrepassKernels[static_cast<size_t>(entry.pso)] =
				BuildForwardKernel(device, entry.config);
		}
	}

	void
	ForwardPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		auto desc = PassDesc();

		desc.SetName(std::format("Forward {}", draw.drawIdx))
			.AddTextureArg(
				TextureArg{ draw.backBufferName,
		                    BarrierSyncFlag::kRenderTarget,
		                    BarrierAccessFlag::kRenderTarget,
		                    BarrierLayout::kRenderTarget })
			.AddBufferArg(
				BufferArg{ std::string(c_DispatchArgsBuffer),
		                   BarrierSyncFlag::kIndirectArgument,
		                   BarrierAccessFlag::kIndirectArgument })
			.AddBufferArg(
				BufferArg{ std::string(c_SortedTransparentBuffer),
		                   BarrierSyncFlag::kVertexShader,
		                   BarrierAccessFlag::kUnorderedAccess })
			.AddBufferArg(
				BufferArg{ std::string(c_TransparentArgsBuffer),
		                   BarrierSyncFlag::kIndirectArgument,
		                   BarrierAccessFlag::kIndirectArgument });

		for (const auto& binding : c_ForwardDataBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}

		for (const auto& binding : c_MaterialBuffers)
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
			auto& forwardData = *foundForwardData;

			for (const SceneBuffer& binding : c_ForwardDataBuffers)
			{
				const auto handle = resources.GetBuffer(binding.graphName);

				auto uniform = forwardData[binding.uniformKey];
				if (uniform.IsValid())
				{
					uniform = handle;
				}
				else
				{
					gfatal(
						"{} key doesn't exist in uniforms. Most likely an error",
						binding.uniformKey);
				}
			}

			forwardData["viewProj"] = draw.viewProj;
		}

		if (auto foundMatData = kernel.FindUniforms("materialData"))
		{
			auto& matData = *foundMatData;
			for (const auto& binding : c_MaterialBuffers)
			{
				const auto handle  = resources.GetBuffer(binding.graphName);
				auto       uniform = matData[binding.uniformKey];
				if (uniform.IsValid())
				{
					uniform = handle;
				}
			}

			if (auto anisoUniform = matData["anisoLinearWrapSampler"]; anisoUniform.IsValid())
			{
				anisoUniform = draw.anisoLinearWrapSampler;
			}
			if (auto clampUniform = matData["linearClampSampler"]; clampUniform.IsValid())
			{
				clampUniform = draw.linearClampSampler;
			}

			// IBL maps: assigning the RHI TextureHandle writes a descriptor handle into the
			// shader-side handle's sole member.
			if (auto u = matData["irradianceMap"]; u.IsValid())
			{
				u = draw.env.irradiance;
			}
			if (auto u = matData["prefilterMap"]; u.IsValid())
			{
				u = draw.env.prefilter;
			}
			if (auto u = matData["brdfLUT"]; u.IsValid())
			{
				u = draw.env.brdfLut;
			}
			if (auto u = matData["cameraPos"]; u.IsValid())
			{
				u = draw.cameraPos;
			}
			if (auto u = matData["exposure"]; u.IsValid())
			{
				u = draw.exposure;
			}
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

		auto gfxState = MeshletState();
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewport);
		gfxState.frameBuffer = FrameBuffer()
		                           .AddColorAttachment(draw.backBufferHandle)
		                           .SetDepthAttachment(draw.depthBufferHandle);

		const auto dispatchArgs = resources.GetBuffer(c_DispatchArgsBuffer);

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
			if (auto forwardData = kernel.FindUniforms("forwardData"))
			{
				(*forwardData)["psoIndex"]   = static_cast<uint32_t>(pso);
				(*forwardData)["baseSource"] = idl::cBaseSourcePsoPrefixSum;
			}

			gfxState.kernel       = &kernel;
			gfxState.indirectArgs = dispatchArgs;
			cmd->SetMeshletState(gfxState);
			cmd->DispatchMeshIndirect(pso);
		}

		DrawTransparent(draw, resources, gfxState);
	}

	void
	ForwardPass::DrawTransparent(
		const DrawData&    draw,
		const PassContext& resources,
		MeshletState       colorState)
	{
		ICommandList* cmd             = resources.GetCommandList();
		const auto    sortedInstances = resources.GetBuffer(c_SortedTransparentBuffer);
		const auto    partitionArgs   = resources.GetBuffer(c_TransparentArgsBuffer);

		// The sort leaves the list as [self-occluding][plain], each half farthest-first, and both
		// occlude PSOs share one pipeline -- so the whole depth-sorted draw is three dispatches whose
		// counts and bases live entirely on the GPU.
		const auto dispatchPartition =
			[&](MeshletKernel& kernel, MeshletState& state, uint32_t partition) {
				gassert(kernel.pipeline.IsInitialized(), "Pass pipeline must be initialized");

				BindKernel(kernel, draw, resources);
				if (auto forwardData = kernel.FindUniforms("forwardData"))
				{
					(*forwardData)["compactedInstances"] = sortedInstances;
					(*forwardData)["baseSource"]         = idl::cBaseSourceTransparentPartition;
					(*forwardData)["partitionIndex"]     = partition;
				}

				state.kernel       = &kernel;
				state.indirectArgs = partitionArgs;
				cmd->SetMeshletState(state);
				cmd->DispatchMeshIndirect(partition);
			};

		// Depth-only: the self-occluding partition writes its front layer's depth with no colour
		// target, so its Equal-tested colour draw below blends that layer once.
		auto prepassState = MeshletState();
		prepassState.viewportState.AddViewportAndScissorRect(draw.viewport);
		prepassState.frameBuffer = FrameBuffer().SetDepthAttachment(draw.depthBufferHandle);

		dispatchPartition(
			m_PrepassKernels[static_cast<size_t>(PsoType::kTransparentOcclude_StaticMesh_PBR)],
			prepassState,
			idl::cOccludePartition);

		dispatchPartition(
			m_Kernels[static_cast<size_t>(PsoType::kTransparentOcclude_StaticMesh_PBR)],
			colorState,
			idl::cOccludePartition);

		dispatchPartition(
			m_Kernels[static_cast<size_t>(PsoType::kTransparent_StaticMesh_PBR)],
			colorState,
			idl::cPlainPartition);
	}

}
