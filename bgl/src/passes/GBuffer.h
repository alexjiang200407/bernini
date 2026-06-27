#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "fg/FrameGraph.h"
#include "fg/PassDesc.h"
#include "passes/DrawData.h"
#include "pipeline/MeshletPipeline.h"
#include "pipeline/MeshletPipeline_d3d12.h"
#include "resource/FrameBuffer.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"
#include "scene/Scene.h"
#include "types/RenderState.h"
#include "uniforms/Uniforms.h"

namespace bgl
{
	class GBufferPass
	{
	public:
		GBufferPass() = default;
		~GBufferPass() noexcept { logger::trace("~GBufferPass"); }
		GBufferPass(IDevice* device) { Init(device); }

		GBufferPass(const GBufferPass&) noexcept = delete;
		GBufferPass(GBufferPass&&) noexcept      = delete;

		GBufferPass&
		operator=(const GBufferPass&) noexcept = delete;

		GBufferPass&
		operator=(GBufferPass&&) noexcept = delete;

		void
		Release()
		{
			m_Pipeline.Reset();
			m_Uniforms.Reset();
		}

		void
		Init(IDevice* device)
		{
			gassert(device != nullptr, "Device must be initialized");

			auto pipelineDesc = MeshletPipelineDesc();

			pipelineDesc.ampShader = device->CreateShader(
				"./shaders/GBuffer_StaticMesh_ASMain.dxil",
				"GBuffer_StaticMesh",
				"ASMain");

			pipelineDesc.meshShader = device->CreateShader(
				"./shaders/GBuffer_StaticMesh_MSMain.dxil",
				"GBuffer_StaticMesh",
				"MSMain");

			pipelineDesc.pixelShader = device->CreateShader(
				"./shaders/FullscreenRect_PSMain.dxil",
				"GBuffer_PBR",
				"PSMain");

			pipelineDesc.AddRtvFormat(Format::BGRA8_UNORM);
			pipelineDesc.SetDsvFormat(Format::D24S8);
			pipelineDesc.renderState = RenderState()
			                               .SetRasterState(
											   RasterState{}
												   .SetCullMode(RasterCullMode::kNone)
												   .SetFillMode(RasterFillMode::kSolid)
												   .SetDepthClipEnable(true))
			                               .SetDepthStencilState(
											   DepthStencilState{}
												   .SetDepthTestEnable(true)
												   .SetDepthWriteEnable(true)
												   .SetDepthFunc(ComparisonFunc::Less)
												   .SetStencilEnable(false));

			m_Pipeline = device->CreateMeshletPipeline(pipelineDesc);
			m_Uniforms = device->CreateUniforms(m_Pipeline.Get(), "sceneData");
		}

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
		{
			auto desc = PassDesc();

			desc.SetName(std::format("GBuffer_{}", draw.drawIdx))
				.AddTexture(
					TextureArg{ draw.backBufferName,
			                    BarrierSyncFlag::kRenderTarget,
			                    BarrierAccessFlag::kRenderTarget,
			                    BarrierLayout::kRenderTarget });

			for (const SceneBuffer& binding : c_SceneBuffers)
			{
				desc.AddBuffer(
					BufferArg{ std::string(binding.graphName),
				               BarrierSyncFlag::kVertexShader,
				               BarrierAccessFlag::kShaderResource });
			}

			desc.SetExec([this, draw](PassContext& resources) { Execute(draw, resources); });

			fg.AddPass(std::move(desc));
		}

		void
		Execute(const DrawData& draw, PassContext& resources)
		{
			ICommandList* cmd = resources.GetCommandList();

			gassert(cmd != nullptr, "Pass commandlist must be initialized");
			gassert(m_Pipeline.IsInitialized(), "Pass pipeline must be initialized");
			gassert(
				draw.scene->GetInstanceCount() > 0,
				"Scene must have at least one instance to render");

			for (const SceneBuffer& binding : c_SceneBuffers)
			{
				const BufferHandle handle = resources.GetBuffer(std::string(binding.graphName));

				auto uniform = m_Uniforms[std::string(binding.uniformKey)];
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

			m_Uniforms["viewProj"] = draw.viewProj;

			auto gfxState     = MeshletState();
			gfxState.pipeline = m_Pipeline;
			gfxState.viewportState.AddViewportAndScissorRect(draw.viewport);
			gfxState.frameBuffer = FrameBuffer()
			                           .AddColorAttachment(draw.backBufferHandle)
			                           .SetDepthAttachment(draw.depthBufferHandle);
			gfxState.uniforms    = &m_Uniforms;

			cmd->SetMeshletState(gfxState);

			cmd->DispatchMesh(draw.scene->GetInstanceCount(), 1, 1);
		}

	private:
		struct SceneBuffer
		{
			std::string_view graphName;
			std::string_view uniformKey;
			bool             required;
		};

		static constexpr std::array<SceneBuffer, 7> c_SceneBuffers = { {
			{ "scene.instanceBuffer", "instanceBuffer", true },
			{ "scene.meshInstanceBuffer", "meshBuffer", false },
			{ "scene.geomBuffer", "geomBuffer", false },
			{ "scene.meshletBuffer", "meshletBuffer", true },
			{ "scene.vertexMapBuffer", "vertexMapBuffer", true },
			{ "scene.vertexBuffer", "vertexBuffer", true },
			{ "scene.indexBuffer", "indexBuffer", true },
		} };

		MeshletPipelineHandle m_Pipeline;
		Uniforms              m_Uniforms;
	};
}
