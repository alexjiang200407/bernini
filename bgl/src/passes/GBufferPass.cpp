#include "passes/GBufferPass.h"
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

namespace bgl
{
	void
	GBufferPass::Init(IDevice* device)
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

		pipelineDesc.pixelShader =
			device->CreateShader("./shaders/FullscreenRect_PSMain.dxil", "GBuffer_PBR", "PSMain");

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

		m_Kernel = device->CreateMeshletKernel(pipelineDesc);
	}

	void
	GBufferPass::AttachToFrameGraph(FrameGraph& fg, const DrawData& draw)
	{
		auto desc = PassDesc();

		desc.SetName(std::format("GBuffer_{}", draw.drawIdx))
			.AddTextureArg(
				TextureArg{ draw.backBufferName,
		                    BarrierSyncFlag::kRenderTarget,
		                    BarrierAccessFlag::kRenderTarget,
		                    BarrierLayout::kRenderTarget });

		for (const SceneBuffer& binding : c_SceneBuffers)
		{
			desc.AddBufferArg(
				BufferArg{ std::string(binding.graphName),
			               BarrierSyncFlag::kVertexShader,
			               BarrierAccessFlag::kShaderResource });
		}

		desc.SetExec([this, draw](const PassContext& resources) { Execute(draw, resources); });

		fg.AddPass(std::move(desc));
	}

	void
	GBufferPass::Execute(const DrawData& draw, const PassContext& resources)
	{
		ICommandList* cmd = resources.GetCommandList();

		gassert(cmd != nullptr, "Pass commandlist must be initialized");
		gassert(m_Kernel.pipeline.IsInitialized(), "Pass pipeline must be initialized");
		gassert(
			draw.scene->GetInstanceCount() > 0,
			"Scene must have at least one instance to render");

		Uniforms& sceneData = m_Kernel["sceneData"];

		for (const SceneBuffer& binding : c_SceneBuffers)
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

		auto gfxState   = MeshletState();
		gfxState.kernel = &m_Kernel;
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewport);
		gfxState.frameBuffer = FrameBuffer()
		                           .AddColorAttachment(draw.backBufferHandle)
		                           .SetDepthAttachment(draw.depthBufferHandle);

		cmd->SetMeshletState(gfxState);

		cmd->DispatchMesh(draw.scene->GetInstanceCount(), 1, 1);
	}

}
