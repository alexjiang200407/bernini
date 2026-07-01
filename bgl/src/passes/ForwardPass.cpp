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
			bool             required;
		};

		static constexpr std::array<SceneBuffer, 9> c_ForwardDataBuffers = { {
			{ "scene.instanceBuffer",
			  "instanceBuffer",
			  BarrierAccessFlag::kShaderResource,
			  BarrierSyncFlag::kVertexShader,
			  true },
			{ "scene.meshInstanceBuffer",
			  "meshBuffer",
			  BarrierAccessFlag::kShaderResource,
			  BarrierSyncFlag::kVertexShader,
			  false },
			{ "scene.geomBuffer",
			  "geomBuffer",
			  BarrierAccessFlag::kShaderResource,
			  BarrierSyncFlag::kVertexShader,
			  false },
			{ "scene.meshletBuffer",
			  "meshletBuffer",
			  BarrierAccessFlag::kShaderResource,
			  BarrierSyncFlag::kVertexShader,
			  true },
			{ "scene.vertexMapBuffer",
			  "vertexMapBuffer",
			  BarrierAccessFlag::kShaderResource,
			  BarrierSyncFlag::kVertexShader,
			  true },
			{ "scene.vertexBuffer",
			  "vertexBuffer",
			  BarrierAccessFlag::kShaderResource,
			  BarrierSyncFlag::kVertexShader,
			  true },
			{ "scene.indexBuffer",
			  "indexBuffer",
			  BarrierAccessFlag::kShaderResource,
			  BarrierSyncFlag::kVertexShader,
			  true },
			{ "scene.compactedInstances",
			  "compactedInstances",
			  BarrierAccessFlag::kIndirectArgument,
			  BarrierSyncFlag::kIndirectArgument,
			  true },
			{ "compactedInstances.psoPrefixSumBuffer",
			  "psoPrefixSum",
			  BarrierAccessFlag::kShaderResource,
			  BarrierSyncFlag::kVertexShader,
			  true },
		} };

		constexpr auto c_DispatchArgsBuffer = "compactedInstances.compactDispatchArgs"sv;

		constexpr auto c_GeomAmpDxil   = "./shaders/Forward_StaticMesh_ASMain.dxil"sv;
		constexpr auto c_GeomMeshDxil  = "./shaders/Forward_StaticMesh_MSMain.dxil"sv;
		constexpr auto c_GeomSrc       = "Forward_StaticMesh"sv;
		constexpr auto c_PbrPixelDxil  = "./shaders/Forward_PBR_PSMain.dxil"sv;
		constexpr auto c_PbrPixelSrc   = "Forward_PBR"sv;
		constexpr auto c_NullPixelDxil = "./shaders/Forward_Null_PSMain.dxil"sv;
		constexpr auto c_NullPixelSrc  = "Forward_Null"sv;

		struct PsoConfig
		{
			std::string_view pixelDxil;
			std::string_view pixelSrc;
			RasterCullMode   cull;
			bool             depthWrite;
			bool             blend;
		};

		static constexpr std::array<PsoConfig, c_PsoCount> c_Psos = { {
			// kOpaque_StaticMesh_Null
			{ c_NullPixelDxil, c_NullPixelSrc, RasterCullMode::kNone, true, false },
			// kOpaque_StaticMesh_PBR
			{ c_PbrPixelDxil, c_PbrPixelSrc, RasterCullMode::kBack, true, false },
			// kAlphaTest_StaticMesh_PBR
			{ c_PbrPixelDxil, c_PbrPixelSrc, RasterCullMode::kNone, true, false },
			// kTransparent_StaticMesh_PBR
			{ c_PbrPixelDxil, c_PbrPixelSrc, RasterCullMode::kBack, false, true },
		} };

		MeshletKernel
		BuildForwardKernel(IDevice* device, const PsoConfig& cfg)
		{
			auto pipelineDesc = MeshletPipelineDesc();

			pipelineDesc.ampShader =
				device->CreateShader(std::string(c_GeomAmpDxil), std::string(c_GeomSrc), "ASMain");
			pipelineDesc.meshShader =
				device->CreateShader(std::string(c_GeomMeshDxil), std::string(c_GeomSrc), "MSMain");

			pipelineDesc.pixelShader = device->CreateShader(
				std::string(cfg.pixelDxil),
				std::string(cfg.pixelSrc),
				"PSMain");

			pipelineDesc.AddRtvFormat(Format::BGRA8_UNORM);
			pipelineDesc.SetDsvFormat(Format::D24S8);

			auto raster = RasterState();
			raster.SetFillMode(RasterFillMode::kSolid)
				.SetCullMode(cfg.cull)
				.SetDepthClipEnable(true);

			auto depth = DepthStencilState{};
			depth.SetDepthTestEnable(true)
				.SetDepthWriteEnable(cfg.depthWrite)
				.SetDepthFunc(ComparisonFunc::Less)
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
		                   BarrierAccessFlag::kIndirectArgument |
		                       BarrierAccessFlag::kShaderResource });

		for (const SceneBuffer& binding : c_ForwardDataBuffers)
		{
			desc.AddBufferArg(
				BufferArg{ std::string(binding.graphName), binding.sync, binding.access });
		}

		desc.SetExec([this, draw](const PassContext& resources) { Execute(draw, resources); });

		fg.AddPass(std::move(desc));
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

		const BufferHandle dispatchArgs = resources.GetBuffer(std::string(c_DispatchArgsBuffer));

		for (uint16_t pso = 0; pso < c_PsoCount; ++pso)
		{
			MeshletKernel& kernel = m_Kernels[pso];
			gassert(kernel.pipeline.IsInitialized(), "Pass pipeline must be initialized");

			auto& sceneData = kernel["sceneData"];

			for (const SceneBuffer& binding : c_ForwardDataBuffers)
			{
				const BufferHandle handle = resources.GetBuffer(std::string(binding.graphName));

				auto uniform = sceneData[std::string(binding.uniformKey)];
				if (uniform.IsValid())
				{
					uniform = handle;
				}
				else if (binding.required)
				{
					gfatal(
						"{} key doesn't exist in uniforms. Most likely an error",
						binding.uniformKey);
				}
			}

			sceneData["viewProj"] = draw.viewProj;
			sceneData["psoIndex"] = static_cast<uint32_t>(pso);

			auto gfxState   = MeshletState();
			gfxState.kernel = &kernel;
			gfxState.viewportState.AddViewportAndScissorRect(draw.viewport);
			gfxState.frameBuffer  = FrameBuffer()
			                            .AddColorAttachment(draw.backBufferHandle)
			                            .SetDepthAttachment(draw.depthBufferHandle);
			gfxState.indirectArgs = dispatchArgs;

			cmd->SetMeshletState(gfxState);

			cmd->DispatchMeshIndirect(pso);
		}
	}

}
