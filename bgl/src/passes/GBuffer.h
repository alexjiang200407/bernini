#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/FrameBuffer.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"
#include "scene/Scene.h"
#include "types/RenderState.h"

namespace bgl
{
	class GBufferPass
	{
	public:
		GBufferPass() = default;
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
			pipelineDesc.renderState = RenderState{}
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
			m_Uniforms = device->CreateUniforms(m_Pipeline.Get());
		}

		void
		Execute(
			Scene*         scene,
			ICommandQueue* cmdQueue,
			ICommandList*  cmdList,
			FrameBuffer    frameBuffer,
			Viewport       vp)
		{
			gassert(cmdQueue != nullptr, "Pass command queue must be initialized");
			gassert(cmdList != nullptr, "Pass commandlist must be initialized");
			gassert(m_Pipeline.IsInitialized(), "Pass pipeline must be initialized");
			gassert(
				scene->GetInstanceCount() > 0,
				"Scene must have at least one instance to render");

			scene->AttachUniforms(m_Uniforms);

			// TODO: Change this
			glm::vec3 cameraPos   = glm::vec3(0.0f, 0.0f, 20.0f);
			glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
			glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);

			// Target point the camera is looking at
			glm::vec3 target = cameraPos + cameraFront;

			glm::mat4 view = glm::lookAt(cameraPos, target, cameraUp);

			glm::mat4 proj = glm::perspective(
				glm::radians(60.0f),  // fovYDeg
				800.0f / 600.0f,      // aspectRatio
				0.5f,                 // nearZ
				500.0f                // farZ
			);

			m_Uniforms["viewProj"] = proj * view;

			auto gfxState     = MeshletState();
			gfxState.pipeline = m_Pipeline;
			gfxState.viewportState.AddViewportAndScissorRect(vp);
			gfxState.frameBuffer = frameBuffer;
			gfxState.uniforms    = &m_Uniforms;

			cmdList->SetMeshletState(gfxState);

			cmdList->DispatchMesh(scene->GetInstanceCount(), 1, 1);
		}

	private:
		MeshletPipelineHandle m_Pipeline;
		Uniforms              m_Uniforms;
	};
}
